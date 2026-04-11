void* plot(void* input){
	struct rule* rule = input;
	int h, w;
	get_size(rule, &h, &w);

	ncplot_options plot_opts={};
	struct ncdplot* plot = ncdplot_create(rule->window, &plot_opts, 0, 0);
	ncplane_set_userptr(rule->window, plot);
	unsigned long plot_x=0;
	double plot_y;
	char str[128];
	FILE* fp;
	while(1){
		fp = popen(rule->data, "r");
		if(fgets(str, w, fp)==NULL) break;	//exit early if command output ends
		plot_y = strtod(str, NULL);
		ncdplot_add_sample(plot, plot_x, plot_y);
		plot_x++;
		pclose(fp);
		sleep(rule->time);
	}
	return NULL;
}
