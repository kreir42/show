//shows the current time in large block digits, scaled to fill the widget's space
//widget->data is a strftime format limited to digits and ':' (e.g. "%H:%M" or "%H:%M:%S")
void* large_clock(void* input){
	struct widget* widget = input;
	char str[16]; //enough for "HH:MM:SS" and similar
	time_t t;
	struct tm* tm;
	if(widget->time>0){
		while(1){
			t = time(NULL);
			tm = localtime(&t);
			strftime(str, sizeof(str), widget->data, tm);
			str[sizeof(str)-1] = '\0';
			draw_big_string(widget, str);
			sleep(widget->time);
		}
	}else{
		t = time(NULL);
		tm = localtime(&t);
		strftime(str, sizeof(str), widget->data, tm);
		str[sizeof(str)-1] = '\0';
		draw_big_string(widget, str);
	}
	return NULL;
}
