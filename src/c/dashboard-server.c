#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <netinet/in.h>
#include <limits.h>
#include <signal.h>
#include <pthread.h>
#include <sys/times.h>
#include "include/zhelpers.h"

#define MAX_EVENTS 10

int debug = 1;
struct conn_data {
	struct sockaddr_in sockaddr;
	socklen_t socklen;
	int fd;
	char * requestData;
	int requestDataLen;
};

struct monitor_item {
	char tagName[50];
	char tagTitle[255];
	char tagRelated[255];
	char tagStatus[20];
};
int items_len=0;

#define ERR_CRIT_ZMQ_START_FAIL 1
#define ERR_CRIT_HEALTH_FAIL	2
#define ERR_CRIT_SOCK 		3
#define ERR_CRIT_SOCKOPT 	4
#define ERR_CRIT_SOCKBIND 	5
#define ERR_CRIT_SOCKLISTEN	6
#define ERR_CRIT_EPOLL_CREATE	7
#define ERR_CRIT_EPOLL_CTL	8
#define ERR_CRIT_EPOLL_WAIT	9
#define ERR_CRIT_EPOLL_CTLC	10

char ** item_status;

struct responsePtr {
	char method[10];
	char path[255];
	char protocol[10];
	char * filename;
       	void * headerResp;
       	void * bodyResp;
       	void * fbuf;
	int status;
	struct stat stat;
};
// TODO: Cache
void getPtrToFileContent(struct responsePtr * rptr){
	FILE * fp;
	fp = fopen(rptr->filename,"r");
	if ( fp == NULL ) {
		perror("fail to open file");
		rptr->headerResp="HTTP/1.1 500 OK\n\n";
		rptr->bodyResp="Fail to open file";
		rptr->status = 1;
	} else if ( fstat( fileno(fp), &rptr->stat) == -1 ) {
		perror("fail to get file stat");
		rptr->status = 2;
	} else {
		printf("file %s has %i bytes size\n", rptr->filename, rptr->stat.st_size);
		rptr->fbuf = malloc(rptr->stat.st_size);
		// read full file at once
		if ( fread(rptr->fbuf, rptr->stat.st_size, 1, fp) != 1 ) {
			perror("Fail to read file");
			rptr->headerResp="HTTP/1.1 500 OK\n\n";
			rptr->bodyResp="Fail to read dashboard.html";
			rptr->status = 3;
		} else {
			if ( memcmp((char *)&rptr->filename[strlen(rptr->filename)-4], ".css", 4 ) == 0 ){
				rptr->headerResp="HTTP/1.1 200 OK\nContent-Type: text/css\n\n";
			} else {
				rptr->headerResp="HTTP/1.1 200 OK\n\n";
			}
			rptr->bodyResp = rptr->fbuf;
			fclose(fp);
			rptr->status = 0;
		}
	}
}

/**
 * Returns what went wrong code:
 * 	0 - Success
 * 	1 - Invalid path
 * 	2 - File not found
 * */
int detectFileFromPath(struct responsePtr * rptr){
	int debug = 0;
	int validPath = 1;
	int c;
	int i=0;
	char lc=0;
	for ( i=1, c=rptr->path[i]; c != 0; i++, c=rptr->path[i] )
	{
		if ( debug )
			printf("testing path char c=[%c, %i, %x], valid=[%i]\n", c, c ,c, validPath );
		if ( !( validPath = 
			   ( c == '/' && i > 1 )
			|| ( c == '.' && lc != '.' )
			|| ( c >= 48 && c <= 57 )
			|| ( c >= 65 && c <= 90 ) 
			|| ( c >= 61 && c <= 122) ) ) {
			if ( debug )
				printf("*** INVALID USE OF CHAR [%c, %i, %x] in pathname\n", c, c ,c ); fflush(stdout);
			// invalid byte char range or ".." let's block .
			break;
		}
		lc = c;
	}
	if ( debug )
		printf("Try to validate [%s], valid = [%i]\n", rptr->path, validPath); fflush(stdout);
	if ( validPath )
		rptr->filename=(char *)&rptr->path[1];
	return !validPath;
}

int pid;

int cpu_usage = 0;
char memory_usage[20];
char self_status[50];
int fdsize;
int threads;

void * serverStatUpdate(void * data){
	char statusitem[20];
	struct tms tms_buf;
	clock_t last_times = times(&tms_buf);
	clock_t cur_times;
	do
	{
		// TODO update vars for cpu and memory as:
		cur_times = times(&tms_buf);
		char * cputime="CPU usage %i%";
		char fileStatus[30];
		sprintf(fileStatus, "/proc/%i/status\0", pid );
		FILE *fpstatus = fopen( (char *)&fileStatus, "r" );
		char line[4096];
		while ( fgets(line, 4096, fpstatus) != NULL )
		{
			sscanf(line, "%s", statusitem);
			if ( strcmp((char *)&statusitem, "State:") == 0 )
			{
				char state[1];
				char stateName[50];
				sscanf(line, "%s %s %s", statusitem, state, stateName);
				sprintf(self_status, state[0] == 'S' ? "OK" : stateName );
			}
			else if ( strcmp((char *)&statusitem, "Threads:") == 0 )
			{
				sscanf(line, "%s %i", statusitem, &threads);
			}
			else if ( strcmp((char *)&statusitem, "FDSize:") == 0 )
			{
				sscanf(line, "%s %i", statusitem, &fdsize);
			}
			else if ( strcmp((char *)&statusitem, "VmRSS:") == 0 )
			{
				char vmRSS[20];
				char unit[30];
				sscanf(line, "%s %s %s", statusitem, vmRSS, unit);
				sprintf(memory_usage,"%s %s",vmRSS,unit);
			}
		}
		if ( fclose(fpstatus) != 0 ){
			perror("Fail to close /proc/self/status");
		}
		last_times = cur_times;
	} while (sleep(1) == 0) ;
}

int detectEndPoint( struct responsePtr * rptr ){
	int ret = 1; // is not a endpoint by default
	char * buffer[4096];
	if ( strcmp( (const char *) &rptr->path, "/health" ) == 0 )
	{
		printf("Health Check...\n"); fflush(stdout);
		rptr->headerResp="HTTP/1.1 200 OK\nContent-Type: application/json\n\n";
		sprintf((char *)&buffer, "{ \"status\": \"%s\", \"cpu\": \"%i%\", \"memory\": \"%s\", \"fdsize\": %i, \"threads\": %i }\0", self_status, cpu_usage, memory_usage, fdsize, threads);
		rptr->bodyResp=&buffer;
		ret = 0; // Is a endpoint and we got not errors.
	}
       	else if ( strcmp ( (char *) &rptr->path, "/status" ) == 0 )
	{
		rptr->headerResp="HTTP/1.1 200 OK\nContent-Type: application/json\n\n";
		int i;
		if ( items_len == 0 ) {
			sprintf((char *) &buffer, "[]");
		}
	       	else
		{
			buffer[0]=0;
			for( i=0; i< items_len; i++ )
			{
				struct monitor_item * item = (struct monitor_item *) item_status[i];
				char *tagName=(char *)&item->tagName;
				char *tagStatus=(char *)&item->tagStatus;
				char *tagTitle=(char *)&item->tagTitle;
				char *tagRelated=(char *)&item->tagRelated;
				sprintf((char *)&buffer, "%s%s{\"%s\":{\"status\":\"%s\",\"title\":\"%s\",\"related\":\"%s\"}}\n%s", (char *)&buffer, i==0 ? "[" : ",", tagName, tagStatus, tagTitle, tagRelated, i==items_len-1 ? "]" : "");
			}
		}
		rptr->bodyResp=&buffer;
		ret = 0;
	}

	return ret;
}

int writeResponse(struct conn_data * conn_data){
	if ( debug )
		printf("== RAW REQUEST START ==\n%s\n== RAW REQUEST END ==\n", conn_data->requestData);


	struct responsePtr rptr;
	rptr.fbuf = NULL; // init as null to avoid free it at end;
	rptr.filename = NULL;
	rptr.headerResp=0;
	rptr.bodyResp=0;
	sscanf( conn_data->requestData, "%s %s %s", &rptr.method, &rptr.path, &rptr.protocol );

	int ret;
	int detectFileErr = 0;
	int detectEndpointErr;
	if ( strcmp( (const char*) &rptr.method, "GET") == 0 ) {
		if ( strcmp( (const char*) &rptr.path, "/" ) == 0 )
		{
			rptr.filename = "dashboard.html";
		}
		else if (( detectEndpointErr = detectEndPoint( &rptr ) ) == 1 )
				detectFileErr = detectFileFromPath( &rptr );
	}

	if ( !detectFileErr && strcmp( (const char*) &rptr.method, "GET") == 0
	  && rptr.filename != NULL ){
		getPtrToFileContent( &rptr );
		if ( rptr.status != 0 )
		{
			perror("Fail to get file content");
		}
	}
       	else if ( rptr.bodyResp == 0 )
	{
		rptr.headerResp="HTTP/1.1 404 OK\n\n";
		rptr.bodyResp="<!DOCTYPE HTML><html><body>File not found, try <a href=/ />home</a>></body></html>";
	}
	if ( ( ret = write(conn_data->fd, rptr.headerResp, strlen(rptr.headerResp) ) ) == -1 ) {
		perror("Response header");
	}
	int bodylen = rptr.fbuf != NULL ? rptr.stat.st_size : strlen(rptr.bodyResp);
	if ( ( ret = write(conn_data->fd, rptr.bodyResp, bodylen ) ) == -1 ) {
		perror("Response body");
	}
	if ( rptr.fbuf != NULL )
		free(rptr.fbuf);
	return ret;
}

int size_t2int(size_t val) {
	return (val <= INT_MAX) ? (int)((ssize_t)val) : -1;
}

size_t int2size_t(int val) {
	return (val < 0) ? __SIZE_MAX__ : (size_t)((unsigned)val);
}

void * start_zmq(void * arg) {
	int i;
	void *context = zmq_ctx_new();
	char msgType[50];
	char msgTagName[50];
	char msgTagTitle[255];
	char msgTagRelated[255];
	char msgTagValue[20];
	void *responder = zmq_socket (context, ZMQ_REP);
	zmq_bind (responder, "tcp://*:5555");
	printf("zmq responder listening on port 5555...\n"); fflush(stdout);
	for (;;) {
		char *message = s_recv (responder);
		if ( message ){
			printf("ZMQ MESSAGE ARRIVED: [%s]\n", message);fflush(stdout);
			sscanf(message, "%s\t%s\t%s", (char *) msgType, (char *) msgTagName, (char *) msgTagValue);
			if ( strcmp( (char *) msgType, "add" ) == 0 )
			{
				sscanf(message, "%[^\t]\t%[^\t]\t%[^\t]\t%[^\t]s", (char *) msgType, (char *) msgTagName, (char *) msgTagTitle, (char *)msgTagRelated);
				item_status=realloc(item_status,sizeof(void **) * ++items_len);
				item_status[items_len -1]=malloc(sizeof(struct monitor_item));
				struct monitor_item * item = (struct monitor_item *)item_status[items_len-1];
				sprintf((char *)&item->tagName, (char *) &msgTagName);
				sprintf((char *)&item->tagTitle, (char *) &msgTagTitle);
				sprintf((char *)&item->tagRelated, (char *) &msgTagRelated);
				sprintf((char *)&item->tagStatus, "unknown");
			}
			else if( strcmp ( (char *) msgType, "status") == 0)
			{
				// TODO hashmap
				for ( i=0; i<items_len; i++){
					struct monitor_item * item = (struct monitor_item *)item_status[i];
					if ( strcmp( item->tagName, (char *) &msgTagName ) == 0) {
						sprintf((char*) &item->tagStatus, (char *)&msgTagValue);
						break;
					}
				}
			}
			free(message);
			zmq_send (responder, "\004", 5, 0);
		}
	}
	zmq_close (responder);
}

int main(int argc, char argv[])
{
	pid = getpid();
	signal(SIGPIPE, SIG_IGN);

	printf("dashboard-server PID: %i\n", pid);
	// start a thread to monitor current process status, mem and cpu usage
	pthread_t thread_status;
	pthread_attr_t thread_attr_status;
	if ( pthread_attr_init( &thread_attr_status ) != 0 ){
		perror("Health thread attr init failed.");
		exit(ERR_CRIT_HEALTH_FAIL);
	}
	if ( pthread_create( &thread_status, &thread_attr_status, &serverStatUpdate, NULL ) != 0 ){
		perror("Health thread creation failed.");
		exit(ERR_CRIT_HEALTH_FAIL);
	}

	// start a thread to receive zmq monitoring messages
	pthread_t thread_zmq;
	pthread_attr_t thrattr;
	if ( pthread_attr_init( &thrattr ) != 0 ) {
		perror("ZMQ Start failed");
		exit(ERR_CRIT_ZMQ_START_FAIL);
	}
	if ( pthread_create( &thread_zmq, &thrattr, &start_zmq, NULL ) != 0 ) {
		perror("ZMQ Start failed");
		exit(ERR_CRIT_ZMQ_START_FAIL);
	}

	int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 6);
	if ( sockfd == -1 ) {
		perror("Socket create");
		exit(ERR_CRIT_SOCK);
	}
	int yes=1;
	if ( setsockopt(sockfd, SOL_SOCKET ,SO_REUSEADDR , &yes, sizeof(yes) ) == -1 ){
		perror("Socket Option Set");
		exit(ERR_CRIT_SOCKOPT);
	}

	int nfds;
	int socklen=sizeof(struct sockaddr);
	struct sockaddr_in pV4srvAddr;
	pV4srvAddr.sin_family=AF_INET;
	pV4srvAddr.sin_addr.s_addr = INADDR_ANY;
	// pV4srvAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	pV4srvAddr.sin_port=htons(9090);
	// assigning a name to a socket
	if ( bind( sockfd, (struct sockaddr*) &pV4srvAddr, socklen ) == -1 ){
		perror("Socket Bind");
		exit(ERR_CRIT_SOCKBIND);
	}
	if ( listen(sockfd, 4096) == -1 ) {
		perror("Socket Listen");
		exit(ERR_CRIT_SOCKLISTEN);
	}
	
	struct epoll_event ev, events[MAX_EVENTS];
	int epollfd = epoll_create(MAX_EVENTS);
	if ( epollfd == -1 ){
		perror("epoll create");
		exit(ERR_CRIT_EPOLL_CREATE);
	}

	ev.events = EPOLLIN;
	ev.data.fd = sockfd;
	if ( epoll_ctl( epollfd, EPOLL_CTL_ADD, sockfd, &ev) == -1 ) {
		perror("epoll control");
		exit(ERR_CRIT_EPOLL_CTL);
	}
	int n, conn_sock;
	for (;;){
		nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
		if ( nfds == -1 ) {
			perror("epoll wait");
			exit(ERR_CRIT_EPOLL_WAIT);
		}
		for ( n = 0; n < nfds; ++n ){
			if ( events[n].data.fd == sockfd ) {
				struct conn_data * conn_data = malloc(sizeof(struct conn_data));
				conn_sock = accept4(sockfd, (struct sockaddr * ) &conn_data->sockaddr, (socklen_t *)&conn_data->socklen, SOCK_NONBLOCK );
				if ( conn_sock == -1 ) {
					perror("server accept fail");
					continue;
				}
				ev.events = EPOLLIN;
				conn_data->fd = conn_sock;
				conn_data->requestData = NULL;
				conn_data->requestDataLen = 0;
				ev.data.ptr = conn_data;
				if ( epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev ) == -1 ){
					perror("epoll control add client");
					exit(ERR_CRIT_EPOLL_CTLC);
				}
				struct in_addr ipAddr = conn_data->sockaddr.sin_addr;
				char client_ip[17];
				int client_port=(int) ntohs(conn_data->sockaddr.sin_port);
				if ( !inet_ntop(AF_INET, &ipAddr, &client_ip, 17 ) ) {
					perror("client ip detection");
				}
				printf("new client connected %i, %s:%i\n", n, client_ip, client_port );
				fflush(stdout);
			} else {
				struct conn_data * conn_data = events[n].data.ptr;
				int fd = conn_data->fd;
				#define BUF_LEN 4096
				char buf[BUF_LEN];
				ssize_t bytes_readed = read( fd, buf, BUF_LEN-1);
				buf[bytes_readed]=0;
				if ( bytes_readed == -1 ) {
					perror("request read");
				}
				fflush(stdout);
				int oldlen = conn_data->requestDataLen;
				conn_data->requestDataLen += bytes_readed;
				conn_data->requestData = realloc(conn_data->requestData, sizeof(char *) * conn_data->requestDataLen);
				memcpy( &conn_data->requestData[oldlen], buf, bytes_readed);
				if ( bytes_readed < BUF_LEN-1 ) {
					// reading done.
					writeResponse(conn_data);
					close(fd);
					free( conn_data->requestData );
					free( conn_data  );
				}
			}
		}
	}
	return 0;
}
