//shows the current time in large block digits, scaled to fill the widget's space
//rule->data is a strftime format limited to digits and ':' (e.g. "%H:%M" or "%H:%M:%S")
void* large_clock(void* input){
	struct rule* rule = input;
	char str[16]; //enough for "HH:MM:SS" and similar
	time_t t;
	struct tm* tm;
	while(1){
		t = time(NULL);
		tm = localtime(&t);
		strftime(str, sizeof(str), rule->data, tm);
		str[sizeof(str)-1] = '\0';
		draw_big_string(rule, str);
		sleep(rule->time);
	}
	return NULL;
}
