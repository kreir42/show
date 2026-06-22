//sparkline: a scrolling history of the last w samples, one column per sample, drawn as vertical eighth-block bars

//render the sample history. rowbuf is scratch of w*3+1 bytes; col_eighths is scratch of w ints; samples holds w raw values (NAN = no data)
static void sparkline_draw(struct widget* widget, char* rowbuf, int* col_eighths, double* samples, int h, int w, struct plot_data* data){
	double lo, hi;
	if(data->min != data->max){ //fixed range
		lo = data->min; hi = data->max;
	}else{ //auto-scale: track the running min/max across the buffer
		lo = INFINITY; hi = -INFINITY;
		for(int c=0; c<w; c++) if(!isnan(samples[c])){
			if(samples[c]<lo) lo = samples[c];
			if(samples[c]>hi) hi = samples[c];
		}
		if(lo>hi){ lo = 0; hi = 1; } //no data yet: render an empty plot
	}
	double span = hi - lo;
	for(int c=0; c<w; c++){
		if(isnan(samples[c])){ col_eighths[c] = 0; continue; } //blank column
		double f = span>0 ? (samples[c]-lo)/span : 0;
		if(f<0) f = 0; else if(f>1) f = 1; //clamp to [0,1]
		col_eighths[c] = lround(f * h * 8);
	}
	for(int r=0; r<h; r++){
		int from_bottom = h-1-r; //0 == bottom row
		int idx = 0;
		for(int c=0; c<w; c++){
			int full = col_eighths[c]/8, rem = col_eighths[c]%8;
			const char* glyph = (from_bottom < full) ? PLOT_FULL : (from_bottom==full && rem) ? plot_fill_vertical[rem] : " ";
			size_t len = strlen(glyph);
			memcpy(rowbuf+idx, glyph, len);
			idx += len;
		}
		rowbuf[idx] = '\0';
		draw_string(widget, r, 0, rowbuf);
	}
	stage_refresh(widget);
}

//helper macro to create the (samples, col_eights, rowbuf) arrays out of a single malloc
#define SPARKLINE_ALLOC(block, samples, col_eighths, rowbuf, w) \
	char* block = malloc((size_t)(w)*sizeof(double) + (size_t)(w)*sizeof(int) + ((size_t)(w)*3 + 1)); \
	if(!block) return NULL; \
	double* samples = (double*)block; \
	int* col_eighths = (int*)(samples + (w)); \
	char* rowbuf = (char*)(col_eighths + (w)); \
	for(int i=0; i<(w); i++) samples[i] = NAN

//push one sample onto the right of the history, dropping the oldest on the left
static inline void sparkline_push(double* samples, int w, double v){
	memmove(samples, samples+1, (size_t)(w-1)*sizeof(double));
	samples[w-1] = v;
}

//a scrolling history of source values, resampled every widget->time seconds
void* sparkline(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	SPARKLINE_ALLOC(block, samples, col_eighths, rowbuf, w);
	pthread_cleanup_push(free, block); //free on thread cancel
	plot_set_color(widget, data->color, data->bg_color);
	if(widget->time>0){ //time==0 doesn't make sense for a sparkline
		while(1){
			sparkline_push(samples, w, plot_sample(data->source));
			sparkline_draw(widget, rowbuf, col_eighths, samples, h, w, data);
			sleep(widget->time);
		}
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//like sparkline, but the source is launched once and each line it prints pushes a sample. widget->time is ignored
void* sparkline_live(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	SPARKLINE_ALLOC(block, samples, col_eighths, rowbuf, w);
	pthread_cleanup_push(free, block); //free on thread cancel
	plot_set_color(widget, data->color, data->bg_color);
	struct plot_live_resources res = { 0, NULL };
	res.fp = plot_spawn(data->source, &res.pid);
	if(res.fp){
		pthread_cleanup_push(plot_live_cleanup, &res); //kill/reap the child on cancel
		sparkline_draw(widget, rowbuf, col_eighths, samples, h, w, data); //empty plot until the first value arrives
		char line[64];
		while(fgets(line, sizeof(line), res.fp)!=NULL){
			sparkline_push(samples, w, strtod(line, NULL));
			sparkline_draw(widget, rowbuf, col_eighths, samples, h, w, data);
		}
		while(1) pause(); //command exited: keep the final frame until cancelled
		pthread_cleanup_pop(1); //unreachable, balances the push macro
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//like sparkline, but source is a file path; plots the file's last w lines, redrawn whenever its mtime changes (polled every widget->time seconds)
void* sparkline_file(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	SPARKLINE_ALLOC(block, samples, col_eighths, rowbuf, w);
	pthread_cleanup_push(free, block); //free on thread cancel
	plot_set_color(widget, data->color, data->bg_color);
	if(widget->time>0){
		time_t last_mtime = 0;
		struct stat st;
		while(1){
			if(stat(data->source, &st)==0 && st.st_mtime>last_mtime){
				last_mtime = st.st_mtime;
				plot_read_tail(data->source, samples, w); //refill the whole buffer from the file's tail
				sparkline_draw(widget, rowbuf, col_eighths, samples, h, w, data);
			}
			sleep(widget->time);
		}
	}else{
		plot_read_tail(data->source, samples, w);
		sparkline_draw(widget, rowbuf, col_eighths, samples, h, w, data);
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}
