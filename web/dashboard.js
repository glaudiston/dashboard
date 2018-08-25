let cards;
let start=function(){
	cards=document.getElementById("cards");
	update();
	setInterval(update, 5000);
}

function update(){
	let x = new XMLHttpRequest();
	x.open("GET", "/status");
	x.onload=function(d){
		if ( d.target.status == 200 ){
			let r=JSON.parse(d.target.response);
			for ( let i=0; i<r.length; i++ ){
				let k = Object.keys(r[i])[0];
				let o = document.getElementById("status-"+k);
				if ( !o ){
					let li=document.createElement("li");
					let icon = document.createElement("div");
					icon.id="status-"+k;
					li.appendChild(icon);
					let span=document.createElement("span");
					span.appendChild(document.createTextNode(r[i][k].title));
					li.appendChild(span);
					cards.appendChild(li);
				}
				if ( o ){
					o.className="status-"+r[i][k].status;
				}
			}
		} else {
			debugger;
		}
	};
	x.send();
}

