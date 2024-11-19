//shows the result of a shell command
void* external_command(void* input){
	struct rule* rule = input;
	int h, w;
	get_size(rule, &h, &w);
	w++;	//+1 for the NULL terminator

	char* str = malloc(w*sizeof(char));
	FILE* fp;
	unsigned short last;
	while(1){
		fp = popen(rule->data, "r");
		for(unsigned short i=0; i<h; i++){
			if(fgets(str, w, fp)==NULL) break;	//exit early if command output ends
			//if last char is a newline, remove
			last = strlen(str)-1;
			if(str[last]=='\n'){
				str[last] = '\0';
				if(last==0){	//first and only char was a newline
					i--;
					continue;
				}
			}
#ifdef USE_NOTCURSES
			ncplane_putstr_yx(rule->plane, i, 0, str);
#else
			mvwaddstr(rule->window, i, 0, str);
#endif
		}
		pclose(fp);
#ifndef USE_NOTCURSES
		wnoutrefresh(rule->window);
#endif
		sleep(rule->time);
	}
	return NULL;
}
