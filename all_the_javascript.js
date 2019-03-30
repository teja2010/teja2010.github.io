
function toggle_visibility(id) {
	var e = document.getElementById(id);
	if(e.style.display == 'block')
		e.style.display = 'none';     //close
	else
		e.style.display = 'block';   //open
}

function changeall_visibility(changeto) {
	var i;
	for(i=1; i<10; i++) {
		//console.log("1." + i);
		var el = document.getElementById('1.' + i);
		el.style.display = changeto;
	}
}

function open_visibility(id) {
	var e = document.getElementById(id);
	var ss = e.style.display;

	changeall_visibility('none'); //close all

	//console.log("open " + id);
	if(ss == 'none') {
		e.style.display = 'block';      //open
		window.location.href = "#"+id;
	}
}

function afterLoad() {
	changeall_visibility('none');
}

function scrollUp() {
	window.scrollBy(0,-50);
}


