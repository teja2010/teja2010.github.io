
var article_num = 0;
var section_count = 0;

//  none  <--> close
//  block <--> open

function toggle_visibility(id) {
	var e = document.getElementById(id);
	if(e.style.display == 'block')
		e.style.display = 'none';     //close
	else
		e.style.display = 'block';   //open
}

function changeall_visibility(changeto) {
	var i;
	for(i=1; i<=section_count; i++) {
		//console.log("1." + i);
		var el = document.getElementById(article_num + '.' + i);
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
	scrollUp();
}

function afterLoad(an, sc) {
  article_num = an;
  section_count = sc;
	changeall_visibility('none');
}

function scrollUp() {
	window.scrollBy(0,-50);
}


