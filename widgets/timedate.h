//shows the date and time in a user-configured string
void* timedate(void* input){
	struct widget* widget = input;
	int h, w;
	get_size(widget, &h, &w);
	size_t size = w*MB_CUR_MAX + 1; //w columns, up to MB_CUR_MAX bytes each for multi-byte output, +1 for the NULL terminator

	char* str = malloc(size);
	if(!str) return NULL; //check for failed malloc
	pthread_cleanup_push(free, str); //free str on thread cancel

	time_t t;
	struct tm* tm;
	if(widget->time>0){
		while(1){
			t = time(NULL);
			tm = localtime(&t);
			strftime(str, size, widget->data, tm);
			str[size-1] = '\0'; //on overflow, strftime returns 0 and buffer is undefined, so add NULL terminator just in case
			draw_string(widget, 0, 0, str);
			stage_refresh(widget);
			sleep(widget->time);
		}
	}else{
		t = time(NULL);
		tm = localtime(&t);
		strftime(str, size, widget->data, tm);
		str[size-1] = '\0'; //on overflow, strftime returns 0 and buffer is undefined, so add NULL terminator just in case
		draw_string(widget, 0, 0, str);
		stage_refresh(widget);
	}
	pthread_cleanup_pop(1);	//frees str: reached if time<=0, or run as the cancel handler otherwise
	return NULL;
}
