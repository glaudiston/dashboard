#include <signal.h>
#include <pthread.h>
#include "include/zhelpers.h"

#define ERR_CRIT_ZMQ_START_FAIL 1

void ** arr_thread;
void ** arr_thread_attr;

void * start_thread_monitor(void * arg){
}

int main(int argc, char * argv[]){
	if ( argc < 4 ) 
	{
		printf("Use: %s add <tagName> <Value>\n", argv[0]);
		printf("Use: %s status <tagName> \"tag title\" \"related-tag1,related-tag2\" \n", argv[0]);
		printf("Ex1: %s add site \"Site institucional\" \"canais\" \n", argv[0]);
		printf("Ex2: %s status site OK\n", argv[0]);
		exit(1);
	}
	signal(SIGPIPE, SIG_IGN);
	char tagType[20];
	char tagName[50];
	char tagTitle[250];
	char tagRelatedTags[250];
	char tagValue[20];
	sprintf((char *)&tagType, argv[1]);
	sprintf((char *)&tagName, argv[2]);
	sprintf((char *)&tagValue, argv[3]);
	void * zmq_context = zmq_ctx_new();
	void * zmq_sender = zmq_socket(zmq_context, ZMQ_REQ);
	zmq_connect(zmq_sender, "tcp://0.0.0.0:5555");
	char msg[4096];
	if ( strcmp((char *) &tagType, "add" ) == 0 )
	{
		sprintf(tagTitle, argv[3]);
		sprintf(tagRelatedTags, argv[4]);
		sprintf(msg, "%s\t%s\t%s\t%s", (char *) &tagType, (char *) &tagName, (char *) &tagTitle, (char *) &tagRelatedTags);
	}
	else if ( strcmp((char *) &tagType, "status" ) == 0 )
	{
		sprintf(msg, "%s\t%s\t%s", (char *) &tagType, (char *) &tagName, (char *) &tagValue);
	}
	else
	{
		sprintf(msg, "ping");
	}
	zmq_send(zmq_sender,msg,strlen(msg), 0);
	char buffer[10];
	zmq_recv (zmq_sender, buffer, 10, 0);
	printf("response [%s]\n", buffer); fflush(stdout);
	int itemsToMonitor = 1;
	// For each monitoring item create a thread
	return 0;
	int i;
	for ( i = 0; i < itemsToMonitor; i++ ) {
		arr_thread = realloc(arr_thread, sizeof(void **) * i);
		arr_thread[i] = malloc(sizeof(pthread_t));
		pthread_attr_t thrattr;
		if ( pthread_attr_init( &thrattr ) != 0 ) {
			perror("ZMQ Start failed");
			exit(ERR_CRIT_ZMQ_START_FAIL);
		}
		if ( pthread_create( (pthread_t *)&arr_thread[i], &thrattr, &start_thread_monitor, NULL ) != 0 ) {
			perror("ZMQ Start failed");
			exit(ERR_CRIT_ZMQ_START_FAIL);
		}
	}

	// wait all threads to die
	for ( i = 0; i < itemsToMonitor; i++ ){
		char * threadExitCode;
		if ( pthread_join( (pthread_t) arr_thread[i], (void **)&threadExitCode) == 0 ) {
			warn("error to join thread %i", i);
		}
		if ( threadExitCode !=0 ) {
			warn("The thread %i has abnormal end error code %i", i, threadExitCode);
		}
		free( threadExitCode );
	}

	return 0;
}

