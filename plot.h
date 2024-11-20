void* plot(void* input){
	struct rule* rule = input;
	int h, w;
	get_size(rule, &h, &w);

#ifdef USE_NOTCURSES
	ncplot_options plot_opts={};
	struct ncdplot* plot = ncdplot_create(rule->plane, &plot_opts, 0, 0);
	ncplane_set_userptr(rule->plane, plot);
	unsigned long plot_x=0;
#else
#endif
	double plot_y;
	char str[128];
	FILE* fp;
	while(1){
		fp = popen(rule->data, "r");
		if(fgets(str, w, fp)==NULL) break;	//exit early if command output ends
		plot_y = strtod(str, NULL);
#ifdef USE_NOTCURSES
		ncdplot_add_sample(plot, plot_x, plot_y);
		plot_x++;
#else
#endif
		pclose(fp);
		sleep(rule->time);
	}
	return NULL;
}
