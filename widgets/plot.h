#ifdef USE_NOTCURSES
void* plot(void* input){
	struct rule* rule = input;
	int h, w;
	get_size(rule, &h, &w);

	ncplot_options plot_opts={};
	struct ncdplot* plot = ncdplot_create(rule->window, &plot_opts, 0, 0);
	ncplane_set_userptr(rule->window, plot);
	unsigned long plot_x=0;
	double plot_y;
	char* str = malloc(w*sizeof(char));
	if(!str) return NULL; //check for failed malloc
	pthread_cleanup_push(free, str); //free str on thread cancel
	FILE* fp;
	while(1){
		fp = popen(rule->data, "r");
		if(fp == NULL) break; //popen failed
		char* line = fgets(str, w, fp);
		pclose(fp);
		if(line == NULL) break;	//exit early if command output ends
		plot_y = strtod(str, NULL);
		ncdplot_add_sample(plot, plot_x, plot_y);
		plot_x++;
		sleep(rule->time);
	}
	pthread_cleanup_pop(1); //frees str on break or cancel
	return NULL;
}
#endif
