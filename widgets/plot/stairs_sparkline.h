//stairs_sparkline: the same rolling time-series as bar_sparkline, but drawn as a connected line of box-drawing glyphs (whole-cell vertical resolution). shares the history buffer, scaling, axes and the poll/live/file flavours; only the draw differs

//line segments, named by the two directions each glyph opens toward
#define LINE_H  "─"
#define LINE_V  "│"
#define LINE_DR "╭"
#define LINE_DL "╮"
#define LINE_UR "╰"
#define LINE_UL "╯"

//render the sample history as a connected line into pr. rows is scratch of pr.w ints (each sample's screen row, -1 = no point). rowbuf is pr.w*3+1 bytes (also reused for the baseline). col_seconds is the time each column spans (0 disables X labels)
static void stairs_sparkline_draw(struct widget* widget, char* rowbuf, int* rows, double* samples, struct plot_region pr, struct plot_data* data, double col_seconds){
	int w = pr.w, h = pr.h;
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
		if(isnan(samples[c])){ rows[c] = -1; continue; } //no point in this column (breaks the line)
		double f = span>0 ? (samples[c]-lo)/span : 0;
		if(f<0) f = 0; else if(f>1) f = 1; //clamp to [0,1]
		rows[c] = (int)lround((1.0-f) * (h-1)); //row 0 = top (max), row h-1 = bottom (min)
	}
	plot_color_on(widget, data->color, data->bg_color); //the plot area carries the color; axes/labels stay terminal-default
	for(int r=0; r<h; r++){
		int idx = 0;
		for(int c=0; c<w; c++){
			int yc = rows[c];
			const char* glyph = " ";
			if(yc>=0){
				int yp = (c>0) ? rows[c-1] : -1; //the segment from the previous sample is drawn in this column
				if(yp<0 || yp==yc){ //start of a run, or flat: a horizontal stroke at the sample's row
					if(r==yc) glyph = LINE_H;
				}else{ //sloped: corners at both ends, a vertical run between
					int up = yc < yp; //current sample sits higher on screen (smaller row)
					int top = up ? yc : yp, bot = up ? yp : yc;
					if(r==yc) glyph = up ? LINE_DR : LINE_UR;
					else if(r==yp) glyph = up ? LINE_UL : LINE_DL;
					else if(r>top && r<bot) glyph = LINE_V;
				}
			}
			size_t len = strlen(glyph);
			memcpy(rowbuf+idx, glyph, len);
			idx += len;
		}
		rowbuf[idx] = '\0';
		draw_string(widget, r, pr.left, rowbuf);
	}
	plot_color_off(widget, data->color, data->bg_color);
	if(pr.left) plot_draw_y_axis(widget, pr.left, h, lo, hi, data->flags);
	if(pr.bottom){ //plot rows drawn, so rowbuf is free to reuse as the baseline scratch
		plot_draw_x_axis(widget, rowbuf, pr.left, w, h);
		if((data->flags & LABEL_X_AXIS) && pr.bottom>=2 && col_seconds>0){ //time labels only make sense when each column spans a known interval
			char lspan[32];
			plot_format_span(lround(col_seconds * w), lspan, sizeof(lspan));
			plot_draw_x_labels(widget, pr.left, w, h, lspan, "0");
		}
	}
	stage_refresh(widget);
}

//a scrolling line of source values, resampled every widget->time seconds
void* stairs_sparkline(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	PLOT_HISTORY_ALLOC(block, samples, rows, rowbuf, pr.w);
	pthread_cleanup_push(free, block); //free on thread cancel
	if(widget->time>0){ //time==0 doesn't make sense for a time series
		while(1){
			plot_history_push(samples, pr.w, plot_sample(data->source));
			stairs_sparkline_draw(widget, rowbuf, rows, samples, pr, data, widget->time); //each column spans widget->time seconds, so the X axis can be labeled
			sleep(widget->time);
		}
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//like stairs_sparkline, but the source is launched once and each line it prints pushes a sample. widget->time is ignored
void* stairs_sparkline_live(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	PLOT_HISTORY_ALLOC(block, samples, rows, rowbuf, pr.w);
	pthread_cleanup_push(free, block); //free on thread cancel
	struct plot_live_resources res = { 0, NULL };
	res.fp = plot_spawn(data->source, &res.pid);
	if(res.fp){
		pthread_cleanup_push(plot_live_cleanup, &res); //kill/reap the child on cancel
		stairs_sparkline_draw(widget, rowbuf, rows, samples, pr, data, 0); //empty plot until the first value arrives; cadence is unknown so no X labels
		char line[64];
		while(fgets(line, sizeof(line), res.fp)!=NULL){
			plot_history_push(samples, pr.w, strtod(line, NULL));
			stairs_sparkline_draw(widget, rowbuf, rows, samples, pr, data, 0);
		}
		while(1) pause(); //command exited: keep the final frame until cancelled
		pthread_cleanup_pop(1); //unreachable, balances the push macro
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//like stairs_sparkline, but source is a file path; plots the file's last pr.w lines, redrawn whenever its mtime changes (polled every widget->time seconds)
void* stairs_sparkline_file(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	PLOT_HISTORY_ALLOC(block, samples, rows, rowbuf, pr.w);
	pthread_cleanup_push(free, block); //free on thread cancel
	if(widget->time>0){
		time_t last_mtime = 0;
		struct stat st;
		while(1){
			if(stat(data->source, &st)==0 && st.st_mtime>last_mtime){
				last_mtime = st.st_mtime;
				plot_read_tail(data->source, samples, pr.w); //refill the whole buffer from the file's tail
				stairs_sparkline_draw(widget, rowbuf, rows, samples, pr, data, 0); //file lines aren't a fixed time interval, so no X labels
			}
			sleep(widget->time);
		}
	}else{
		plot_read_tail(data->source, samples, pr.w);
		stairs_sparkline_draw(widget, rowbuf, rows, samples, pr, data, 0);
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}
