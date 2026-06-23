//braille sparklines: the same rolling time-series as bar_sparkline, plotted on a braille dot canvas. a cell is 2x4 dots, and we use both dot-columns: the history holds 2 samples per cell (2*w samples, twice the horizontal resolution of the other sparklines). three render styles share all the machinery, differing only in how each column's dots are filled:
//- braille_line_sparkline: each column is filled vertically to the previous sample's level, forming a connected line (samples are one dot-column apart, so there is no room to interpolate)
//- braille_scatter_sparkline: each sample is a single dot, leaving the points unconnected
//- braille_bar_sparkline: each column is filled from the baseline up to the sample (filled bars like bar_sparkline, but with 4 dot rows of vertical resolution instead of 8 eighth-block levels)
//all share the scaling, axes and poll/live/file flavours; only the sample buffer and this draw differ from the other sparklines

//light every braille dot in column dot_x between dot rows y0 and y1 (inclusive). each cell is 2 dots wide x 4 tall; the bit values are fixed by the Unicode braille block
static void braille_vline(unsigned char* cells, int w, int dot_x, int y0, int y1){
	static const unsigned char dot[2][4] = { {0x01,0x02,0x04,0x40}, {0x08,0x10,0x20,0x80} };
	if(y0>y1){ int t=y0; y0=y1; y1=t; }
	int col = dot_x/2, sx = dot_x&1;
	for(int y=y0; y<=y1; y++) cells[(y/4)*w + col] |= dot[sx][y&3];
}

//allocate the braille scratch out of one malloc: the rolling sample buffer (2*w doubles, one per dot-column), the cell dot grid (w*h bytes) and a row string (w*3+1 bytes, braille glyphs are 3 bytes each)
#define BRAILLE_ALLOC(block, samples, cells, rowbuf, w, h) \
	char* block = malloc((size_t)2*(w)*sizeof(double) + (size_t)(w)*(h) + ((size_t)(w)*3 + 1)); \
	if(!block) return NULL; \
	double* samples = (double*)block; \
	unsigned char* cells = (unsigned char*)(samples + 2*(w)); \
	char* rowbuf = (char*)(cells + (size_t)(w)*(h)); \
	for(int i=0; i<2*(w); i++) samples[i] = NAN

enum braille_style{ BRAILLE_LINE, BRAILLE_SCATTER, BRAILLE_BAR };

//render the sample history into pr. samples holds 2*pr.w values. cells is scratch of pr.w*pr.h bytes. rowbuf is pr.w*3+1 bytes. col_seconds is the time each sample spans (0 disables X labels). style determines how each column is filled
static void braille_sparkline_draw(struct widget* widget, char* rowbuf, unsigned char* cells, double* samples, struct plot_region pr, struct plot_data* data, double col_seconds, enum braille_style style){
	int w = pr.w, h = pr.h, n = 2*w; //n = sample count, or canvas width in dots
	double lo, hi;
	if(data->min != data->max){ //fixed range
		lo = data->min; hi = data->max;
	}else{ //auto-scale: track the running min/max across the buffer
		lo = INFINITY; hi = -INFINITY;
		for(int c=0; c<n; c++) if(!isnan(samples[c])){
			if(samples[c]<lo) lo = samples[c];
			if(samples[c]>hi) hi = samples[c];
		}
		if(lo>hi){ lo = 0; hi = 1; } //no data yet: render an empty plot
	}
	double span = hi - lo;
	memset(cells, 0, (size_t)w*h); //clear the dot grid for this frame
	int H = 4*h; //canvas height in dots
	int have_prev = 0, py = 0;
	for(int c=0; c<n; c++){
		if(isnan(samples[c])){ have_prev = 0; continue; } //gap: break the line
		double f = span>0 ? (samples[c]-lo)/span : 0;
		if(f<0) f = 0; else if(f>1) f = 1; //clamp to [0,1]
		int y = (int)lround((1.0-f) * (H-1)); //one sample per dot-column; dot 0 = top (max), dot H-1 = bottom (min)
		//the column is filled over [y0,y1] (braille_vline sorts them). default is a single dot at y (scatter / lone point)
		int y0 = y, y1 = y;
		if(style==BRAILLE_LINE && have_prev) y0 = py; //line: fill from the previous sample's level to this one
		else if(style==BRAILLE_BAR) y1 = H-1; //bar: fill from this level down to the baseline
		braille_vline(cells, w, c, y0, y1);
		py = y; have_prev = 1;
	}
	plot_color_on(widget, data->color, data->bg_color); //the plot area carries the color; axes/labels stay terminal-default
	for(int r=0; r<h; r++){
		int idx = 0;
		for(int c=0; c<w; c++){
			unsigned char b = cells[r*w + c];
			if(b==0){ rowbuf[idx++] = ' '; } //empty cell
			else{ //U+2800 + bits, encoded as 3 UTF-8 bytes
				rowbuf[idx++] = (char)0xE2;
				rowbuf[idx++] = (char)(0xA0 | (b>>6));
				rowbuf[idx++] = (char)(0x80 | (b&0x3F));
			}
		}
		rowbuf[idx] = '\0';
		draw_string(widget, r, pr.left, rowbuf);
	}
	plot_color_off(widget, data->color, data->bg_color);
	if(pr.left) plot_draw_y_axis(widget, pr.left, h, lo, hi, data->flags);
	if(pr.bottom){ //plot rows drawn, so rowbuf is free to reuse as the baseline scratch
		plot_draw_x_axis(widget, rowbuf, pr.left, w, h);
		if((data->flags & LABEL_X_AXIS) && pr.bottom>=2 && col_seconds>0){ //time labels only make sense when each sample spans a known interval
			char lspan[16];
			plot_format_span(lround(col_seconds * n), lspan, sizeof(lspan)); //n samples across the width
			plot_draw_x_labels(widget, pr.left, w, h, lspan, "0");
		}
	}
	stage_refresh(widget);
}

//a scrolling braille line of source values, resampled every widget->time seconds
void* braille_line_sparkline(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	BRAILLE_ALLOC(block, samples, cells, rowbuf, pr.w, pr.h);
	pthread_cleanup_push(free, block); //free on thread cancel
	if(widget->time>0){ //time==0 doesn't make sense for a time series
		while(1){
			plot_history_push(samples, 2*pr.w, plot_sample(data->source)); //2*pr.w samples, one per dot-column
			braille_sparkline_draw(widget, rowbuf, cells, samples, pr, data, widget->time, BRAILLE_LINE); //each sample spans widget->time seconds, so the X axis can be labeled
			sleep(widget->time);
		}
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//like braille_line_sparkline, but the source is launched once and each line it prints pushes a sample. widget->time is ignored
void* braille_line_sparkline_live(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	BRAILLE_ALLOC(block, samples, cells, rowbuf, pr.w, pr.h);
	pthread_cleanup_push(free, block); //free on thread cancel
	struct plot_live_resources res = { 0, NULL };
	res.fp = plot_spawn(data->source, &res.pid);
	if(res.fp){
		pthread_cleanup_push(plot_live_cleanup, &res); //kill/reap the child on cancel
		braille_sparkline_draw(widget, rowbuf, cells, samples, pr, data, 0, BRAILLE_LINE); //empty plot until the first value arrives; cadence is unknown so no X labels
		char line[64];
		while(fgets(line, sizeof(line), res.fp)!=NULL){
			plot_history_push(samples, 2*pr.w, strtod(line, NULL));
			braille_sparkline_draw(widget, rowbuf, cells, samples, pr, data, 0, BRAILLE_LINE);
		}
		while(1) pause(); //command exited: keep the final frame until cancelled
		pthread_cleanup_pop(1); //unreachable, balances the push macro
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//like braille_line_sparkline, but source is a file path; plots the file's last 2*pr.w lines, redrawn whenever its mtime changes (polled every widget->time seconds)
void* braille_line_sparkline_file(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	BRAILLE_ALLOC(block, samples, cells, rowbuf, pr.w, pr.h);
	pthread_cleanup_push(free, block); //free on thread cancel
	if(widget->time>0){
		time_t last_mtime = 0;
		struct stat st;
		while(1){
			if(stat(data->source, &st)==0 && st.st_mtime>last_mtime){
				last_mtime = st.st_mtime;
				plot_read_tail(data->source, samples, 2*pr.w); //refill the whole buffer (2 samples per cell) from the file's tail
				braille_sparkline_draw(widget, rowbuf, cells, samples, pr, data, 0, BRAILLE_LINE); //file lines aren't a fixed time interval, so no X labels
			}
			sleep(widget->time);
		}
	}else{
		plot_read_tail(data->source, samples, 2*pr.w);
		braille_sparkline_draw(widget, rowbuf, cells, samples, pr, data, 0, BRAILLE_LINE);
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//like braille_line_sparkline, but the samples are left unconnected (a scatter of single dots). resampled every widget->time seconds
void* braille_scatter_sparkline(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	BRAILLE_ALLOC(block, samples, cells, rowbuf, pr.w, pr.h);
	pthread_cleanup_push(free, block); //free on thread cancel
	if(widget->time>0){ //time==0 doesn't make sense for a time series
		while(1){
			plot_history_push(samples, 2*pr.w, plot_sample(data->source)); //2*pr.w samples, one per dot-column
			braille_sparkline_draw(widget, rowbuf, cells, samples, pr, data, widget->time, BRAILLE_SCATTER);
			sleep(widget->time);
		}
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//like braille_scatter_sparkline, but the source is launched once and each line it prints pushes a sample. widget->time is ignored
void* braille_scatter_sparkline_live(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	BRAILLE_ALLOC(block, samples, cells, rowbuf, pr.w, pr.h);
	pthread_cleanup_push(free, block); //free on thread cancel
	struct plot_live_resources res = { 0, NULL };
	res.fp = plot_spawn(data->source, &res.pid);
	if(res.fp){
		pthread_cleanup_push(plot_live_cleanup, &res); //kill/reap the child on cancel
		braille_sparkline_draw(widget, rowbuf, cells, samples, pr, data, 0, BRAILLE_SCATTER); //empty plot until the first value arrives; cadence is unknown so no X labels
		char line[64];
		while(fgets(line, sizeof(line), res.fp)!=NULL){
			plot_history_push(samples, 2*pr.w, strtod(line, NULL));
			braille_sparkline_draw(widget, rowbuf, cells, samples, pr, data, 0, BRAILLE_SCATTER);
		}
		while(1) pause(); //command exited: keep the final frame until cancelled
		pthread_cleanup_pop(1); //unreachable, balances the push macro
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//like braille_scatter_sparkline, but source is a file path; plots the file's last 2*pr.w lines, redrawn whenever its mtime changes (polled every widget->time seconds)
void* braille_scatter_sparkline_file(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	BRAILLE_ALLOC(block, samples, cells, rowbuf, pr.w, pr.h);
	pthread_cleanup_push(free, block); //free on thread cancel
	if(widget->time>0){
		time_t last_mtime = 0;
		struct stat st;
		while(1){
			if(stat(data->source, &st)==0 && st.st_mtime>last_mtime){
				last_mtime = st.st_mtime;
				plot_read_tail(data->source, samples, 2*pr.w); //refill the whole buffer (2 samples per cell) from the file's tail
				braille_sparkline_draw(widget, rowbuf, cells, samples, pr, data, 0, BRAILLE_SCATTER); //file lines aren't a fixed time interval, so no X labels
			}
			sleep(widget->time);
		}
	}else{
		plot_read_tail(data->source, samples, 2*pr.w);
		braille_sparkline_draw(widget, rowbuf, cells, samples, pr, data, 0, BRAILLE_SCATTER);
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//like bar_sparkline, but on the braille grid: each column is filled from the baseline up to the sample (2 samples per cell, 4 dot rows of vertical resolution). resampled every widget->time seconds
void* braille_bar_sparkline(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	BRAILLE_ALLOC(block, samples, cells, rowbuf, pr.w, pr.h);
	pthread_cleanup_push(free, block); //free on thread cancel
	if(widget->time>0){ //time==0 doesn't make sense for a time series
		while(1){
			plot_history_push(samples, 2*pr.w, plot_sample(data->source)); //2*pr.w samples, one per dot-column
			braille_sparkline_draw(widget, rowbuf, cells, samples, pr, data, widget->time, BRAILLE_BAR);
			sleep(widget->time);
		}
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//like braille_bar_sparkline, but the source is launched once and each line it prints pushes a sample. widget->time is ignored
void* braille_bar_sparkline_live(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	BRAILLE_ALLOC(block, samples, cells, rowbuf, pr.w, pr.h);
	pthread_cleanup_push(free, block); //free on thread cancel
	struct plot_live_resources res = { 0, NULL };
	res.fp = plot_spawn(data->source, &res.pid);
	if(res.fp){
		pthread_cleanup_push(plot_live_cleanup, &res); //kill/reap the child on cancel
		braille_sparkline_draw(widget, rowbuf, cells, samples, pr, data, 0, BRAILLE_BAR); //empty plot until the first value arrives; cadence is unknown so no X labels
		char line[64];
		while(fgets(line, sizeof(line), res.fp)!=NULL){
			plot_history_push(samples, 2*pr.w, strtod(line, NULL));
			braille_sparkline_draw(widget, rowbuf, cells, samples, pr, data, 0, BRAILLE_BAR);
		}
		while(1) pause(); //command exited: keep the final frame until cancelled
		pthread_cleanup_pop(1); //unreachable, balances the push macro
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//like braille_bar_sparkline, but source is a file path; plots the file's last 2*pr.w lines, redrawn whenever its mtime changes (polled every widget->time seconds)
void* braille_bar_sparkline_file(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	BRAILLE_ALLOC(block, samples, cells, rowbuf, pr.w, pr.h);
	pthread_cleanup_push(free, block); //free on thread cancel
	if(widget->time>0){
		time_t last_mtime = 0;
		struct stat st;
		while(1){
			if(stat(data->source, &st)==0 && st.st_mtime>last_mtime){
				last_mtime = st.st_mtime;
				plot_read_tail(data->source, samples, 2*pr.w); //refill the whole buffer (2 samples per cell) from the file's tail
				braille_sparkline_draw(widget, rowbuf, cells, samples, pr, data, 0, BRAILLE_BAR); //file lines aren't a fixed time interval, so no X labels
			}
			sleep(widget->time);
		}
	}else{
		plot_read_tail(data->source, samples, 2*pr.w);
		braille_sparkline_draw(widget, rowbuf, cells, samples, pr, data, 0, BRAILLE_BAR);
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}
