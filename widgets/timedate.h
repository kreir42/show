//shows the date and time in a user-configured string
void* timedate(void* input){
	struct rule* rule = input;
	int h, w;
	get_size(rule, &h, &w);
	size_t size = w*MB_CUR_MAX + 1; //w columns, up to MB_CUR_MAX bytes each for multi-byte output, +1 for the NULL terminator

	char* str = malloc(size);
	if(!str) return NULL; //check for failed malloc
	pthread_cleanup_push(free, str); //free str on thread cancel

	time_t t;
	struct tm* tm;
	while(1){
		t = time(NULL);
		tm = localtime(&t);
		strftime(str, size, rule->data, tm);
		str[size-1] = '\0'; //on overflow, strftime returns 0 and buffer is undefined, so add NULL terminator just in case
		draw_string(rule, 0, 0, str);
		stage_refresh(rule);
		sleep(rule->time);
	}
	pthread_cleanup_pop(1);	//unreachable, balances pthread_cleanup_push macro
	return NULL;
}
