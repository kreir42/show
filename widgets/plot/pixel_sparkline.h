//pixel sparklines (notcurses only): the same rolling time-series as the other sparklines, but rasterized into an RGBA bitmap and blitted with the pixel blitter. sub-cell resolution in both axes and true sloped line segments rather than a dot grid. two render styles:
//- pixel_line_sparkline: consecutive samples joined by sloped segments, with linear interpolation between them
//- pixel_steps_sparkline: each sample held flat until the next, joined by vertical steps (no interpolation)
//history is one sample per cell column (pr.w). the bitmap gets its own dedicated child plane over the plot region while the axes/labels stay as cell text on the widget's plane. pixel support is probed once at startup (see pixel_support); if the terminal can't blit pixels these widgets render nothing at all and the thread returns immediately
#ifdef USE_NOTCURSES

enum pixel_style{ PIXEL_LINE, PIXEL_STEPS };

//write one opaque/transparent RGBA pixel at (x,y) into a pw-wide buffer
static inline void pixel_put(unsigned char* buf, int pw, int x, int y, int r, int g, int b){
	unsigned char* p = buf + ((size_t)y*pw + x)*4;
	p[0] = r; p[1] = g; p[2] = b;
	p[3] = 255; //opaque
}

//draw the vertical span [ya,yb] of column x in one opaque color (connects consecutive samples so the line has no gaps where it climbs steeply)
static void pixel_vspan(unsigned char* buf, int pw, int x, int ya, int yb, int r, int g, int b){
	if(ya>yb){ int t=ya; ya=yb; yb=t; }
	for(int y=ya; y<=yb; y++) pixel_put(buf, pw, x, y, r, g, b);
}

//carve the pixel scratch from one malloc: the rolling sample buffer ("count" doubles, one per cell column), the RGBA bitmap (ph*pw*4 bytes) and a row string for the X-axis baseline (count*3+1 bytes). doubles first so alignment holds; samples start NAN
#define PIXEL_ALLOC(block, samples, bitmap, rowbuf, count, ph, pw) \
	char* block = malloc((size_t)(count)*sizeof(double) + (size_t)(ph)*(pw)*4 + ((size_t)(count)*3 + 1)); \
	if(!block) return NULL; \
	double* samples = (double*)block; \
	unsigned char* bitmap = (unsigned char*)(samples + (count)); \
	char* rowbuf = (char*)(bitmap + (size_t)(ph)*(pw)*4); \
	for(int i=0; i<(count); i++) samples[i] = NAN

//pixels per cell for this widget's plane
static void pixel_cell_dims(struct widget* widget, int* cell_py, int* cell_px){
	unsigned pxy, pxx, cdy, cdx, mby, mbx;
	ncplane_pixel_geom(widget->window, &pxy, &pxx, &cdy, &cdx, &mby, &mbx);
	*cell_py = (int)cdy; *cell_px = (int)cdx;
}

//render the sample history into pr. *sprite is the dedicated bitmap plane, created on first use and reused (dropped with every plane on rebuild). samples holds pr.w values. bitmap is pr.h*cell_py by pr.w*cell_px RGBA. rowbuf is pr.w*3+1 bytes for the baseline. col_seconds is the time each sample spans (0 disables X labels). style selects line vs steps
static void pixel_sparkline_draw(struct widget* widget, struct ncplane** sprite, char* rowbuf, unsigned char* bitmap, double* samples, struct plot_region pr, struct plot_data* data, int cell_py, int cell_px, double col_seconds, enum pixel_style style){
	int w = pr.w, h = pr.h, n = w; //one sample per cell column
	double lo, hi;
	if(data->min != data->max){ //fixed range
		lo = data->min; hi = data->max;
	}else{ //auto-scale: running min/max across the buffer
		lo = INFINITY; hi = -INFINITY;
		for(int c=0; c<n; c++) if(!isnan(samples[c])){
			if(samples[c]<lo) lo = samples[c];
			if(samples[c]>hi) hi = samples[c];
		}
		if(lo>hi){ lo = 0; hi = 1; } //no data yet
	}
	double span = hi - lo;

	int ph = h*cell_py, pw = w*cell_px;
	//the bitmap is filled fully opaque so each frame's sprixel completely overwrites the previous one: a transparent sixel redrawn in place does NOT erase the old pixels, so the plot would otherwise accumulate every frame. background is bg_color, or the terminal's own default background so an opaque fill still blends in
	int br, bg, bb;
	if(data->bg_color){ br=(data->bg_color>>16)&0xff; bg=(data->bg_color>>8)&0xff; bb=data->bg_color&0xff; }
	else{ uint32_t d; if(notcurses_default_background(nc, &d)==0){ br=(d>>16)&0xff; bg=(d>>8)&0xff; bb=d&0xff; } else br=bg=bb=0; }
	for(int i=0; i<ph*pw; i++){ unsigned char* p = bitmap + (size_t)i*4; p[0]=br; p[1]=bg; p[2]=bb; p[3]=255; }
	//foreground: the plot color, or the terminal's default foreground
	int fr, fg, fb;
	if(data->color){ fr=(data->color>>16)&0xff; fg=(data->color>>8)&0xff; fb=data->color&0xff; }
	else{ uint32_t d; if(notcurses_default_foreground(nc, &d)==0){ fr=(d>>16)&0xff; fg=(d>>8)&0xff; fb=d&0xff; } else fr=fg=fb=255; }
	int have_prev = 0, prev_y = 0;
	for(int x=0; x<pw; x++){
		double s = (n>1) ? (double)x*(n-1)/(pw-1) : 0; //fractional sample position at this pixel column
		double val; int valid;
		if(style==PIXEL_STEPS){ //nearest sample held flat: equal-width runs make a staircase
			int idx = (int)lround(s); if(idx>n-1) idx = n-1;
			val = samples[idx]; valid = !isnan(val);
		}else{ //line: interpolate between the two bracketing samples
			int i = (int)s;
			if(i>=n-1){ val = samples[n-1]; valid = !isnan(val); }
			else{ double a = samples[i], b = samples[i+1]; valid = !isnan(a) && !isnan(b); val = a + (b-a)*(s-i); }
		}
		if(!valid){ have_prev = 0; continue; } //NaN or a NaN endpoint: break the line
		double f = span>0 ? (val-lo)/span : 0; if(f<0) f=0; else if(f>1) f=1;
		int y = (int)lround((1.0-f) * (ph-1)); //pixel 0 = top (max), ph-1 = bottom (min)
		if(have_prev) pixel_vspan(bitmap, pw, x, prev_y, y, fr, fg, fb); //connect to the previous column
		else pixel_put(bitmap, pw, x, y, fr, fg, fb);
		prev_y = y; have_prev = 1;
	}
	draw_lock(); //the plane create and blit mutate the pile; serialize them against the render loop
	if(*sprite==NULL){ //own plane for the bitmap, positioned over the plot region; a sprixel can't coexist with cell text on one plane
		struct ncplane_options o = { .y = 0, .x = pr.left, .rows = (unsigned)h, .cols = (unsigned)w };
		*sprite = ncplane_create(widget->window, &o);
	}
	if(*sprite){
		struct ncvisual* v = ncvisual_from_rgba(bitmap, ph, pw*4, pw);
		if(v){
			struct ncvisual_options vopts = {
				.n = *sprite, //blit onto the dedicated plane (replaces the previous frame's sprixel)
				.scaling = NCSCALE_NONE, //bitmap is already sized to the region's pixels
				.blitter = NCBLIT_PIXEL,
			};
			ncvisual_blit(nc, v, &vopts);
			ncvisual_destroy(v);
		}
	}
	draw_unlock();
	//axes and labels are cell text in the margins
	if(pr.left) plot_draw_y_axis(widget, pr.left, h, lo, hi, data->flags);
	if(pr.bottom){
		plot_draw_x_axis(widget, rowbuf, pr.left, w, h);
		if((data->flags & LABEL_X_AXIS) && pr.bottom>=2 && col_seconds>0){
			char lspan[16];
			plot_format_span(lround(col_seconds * n), lspan, sizeof(lspan));
			plot_draw_x_labels(widget, pr.left, w, h, lspan, "0");
		}
	}
	stage_refresh(widget);
}

//a scrolling pixel line of source values, resampled every widget->time seconds
void* pixel_line_sparkline(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	if(pixel_support<=0) return NULL; //no pixel blitting, render nothing
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	int cpy, cpx; pixel_cell_dims(widget, &cpy, &cpx);
	int ph = pr.h*cpy, pw = pr.w*cpx;
	struct ncplane* sprite = NULL; //the bitmap's dedicated plane, created on first draw
	PIXEL_ALLOC(block, samples, bitmap, rowbuf, pr.w, ph, pw);
	pthread_cleanup_push(free, block); //free on thread cancel
	if(widget->time>0){ //time==0 doesn't make sense for a time series
		while(1){
			plot_history_push(samples, pr.w, plot_sample(data->source));
			pixel_sparkline_draw(widget, &sprite, rowbuf, bitmap, samples, pr, data, cpy, cpx, widget->time, PIXEL_LINE);
			sleep(widget->time);
		}
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//like pixel_line_sparkline, but the source is launched once and each line it prints pushes a sample. widget->time is ignored
void* pixel_line_sparkline_live(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	if(pixel_support<=0) return NULL; //no pixel blitting, render nothing
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	int cpy, cpx; pixel_cell_dims(widget, &cpy, &cpx);
	int ph = pr.h*cpy, pw = pr.w*cpx;
	struct ncplane* sprite = NULL; //the bitmap's dedicated plane, created on first draw
	PIXEL_ALLOC(block, samples, bitmap, rowbuf, pr.w, ph, pw);
	pthread_cleanup_push(free, block); //free on thread cancel
	struct plot_live_resources res = { 0, NULL };
	res.fp = plot_spawn(data->source, &res.pid);
	if(res.fp){
		pthread_cleanup_push(plot_live_cleanup, &res); //kill/reap the child on cancel
		pixel_sparkline_draw(widget, &sprite, rowbuf, bitmap, samples, pr, data, cpy, cpx, 0, PIXEL_LINE); //empty plot until the first value; cadence unknown so no X labels
		char line[64];
		while(fgets(line, sizeof(line), res.fp)!=NULL){
			plot_history_push(samples, pr.w, strtod(line, NULL));
			pixel_sparkline_draw(widget, &sprite, rowbuf, bitmap, samples, pr, data, cpy, cpx, 0, PIXEL_LINE);
		}
		while(1) pause(); //command exited: keep the final frame until cancelled
		pthread_cleanup_pop(1); //unreachable, balances the push macro
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//like pixel_line_sparkline, but source is a file path; plots the file's last pr.w lines, redrawn whenever its mtime changes (polled every widget->time seconds)
void* pixel_line_sparkline_file(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	if(pixel_support<=0) return NULL; //no pixel blitting, render nothing
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	int cpy, cpx; pixel_cell_dims(widget, &cpy, &cpx);
	int ph = pr.h*cpy, pw = pr.w*cpx;
	struct ncplane* sprite = NULL; //the bitmap's dedicated plane, created on first draw
	PIXEL_ALLOC(block, samples, bitmap, rowbuf, pr.w, ph, pw);
	pthread_cleanup_push(free, block); //free on thread cancel
	if(widget->time>0){
		time_t last_mtime = 0;
		struct stat st;
		while(1){
			if(stat(data->source, &st)==0 && st.st_mtime>last_mtime){
				last_mtime = st.st_mtime;
				plot_read_tail(data->source, samples, pr.w);
				pixel_sparkline_draw(widget, &sprite, rowbuf, bitmap, samples, pr, data, cpy, cpx, 0, PIXEL_LINE); //file lines aren't a fixed interval, so no X labels
			}
			sleep(widget->time);
		}
	}else{
		plot_read_tail(data->source, samples, pr.w);
		pixel_sparkline_draw(widget, &sprite, rowbuf, bitmap, samples, pr, data, cpy, cpx, 0, PIXEL_LINE);
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//like pixel_line_sparkline, but samples are held flat and joined by vertical steps (a staircase). resampled every widget->time seconds
void* pixel_steps_sparkline(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	if(pixel_support<=0) return NULL; //no pixel blitting, render nothing
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	int cpy, cpx; pixel_cell_dims(widget, &cpy, &cpx);
	int ph = pr.h*cpy, pw = pr.w*cpx;
	struct ncplane* sprite = NULL; //the bitmap's dedicated plane, created on first draw
	PIXEL_ALLOC(block, samples, bitmap, rowbuf, pr.w, ph, pw);
	pthread_cleanup_push(free, block); //free on thread cancel
	if(widget->time>0){
		while(1){
			plot_history_push(samples, pr.w, plot_sample(data->source));
			pixel_sparkline_draw(widget, &sprite, rowbuf, bitmap, samples, pr, data, cpy, cpx, widget->time, PIXEL_STEPS);
			sleep(widget->time);
		}
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//like pixel_steps_sparkline, but the source is launched once and each line it prints pushes a sample. widget->time is ignored
void* pixel_steps_sparkline_live(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	if(pixel_support<=0) return NULL; //no pixel blitting, render nothing
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	int cpy, cpx; pixel_cell_dims(widget, &cpy, &cpx);
	int ph = pr.h*cpy, pw = pr.w*cpx;
	struct ncplane* sprite = NULL; //the bitmap's dedicated plane, created on first draw
	PIXEL_ALLOC(block, samples, bitmap, rowbuf, pr.w, ph, pw);
	pthread_cleanup_push(free, block); //free on thread cancel
	struct plot_live_resources res = { 0, NULL };
	res.fp = plot_spawn(data->source, &res.pid);
	if(res.fp){
		pthread_cleanup_push(plot_live_cleanup, &res); //kill/reap the child on cancel
		pixel_sparkline_draw(widget, &sprite, rowbuf, bitmap, samples, pr, data, cpy, cpx, 0, PIXEL_STEPS);
		char line[64];
		while(fgets(line, sizeof(line), res.fp)!=NULL){
			plot_history_push(samples, pr.w, strtod(line, NULL));
			pixel_sparkline_draw(widget, &sprite, rowbuf, bitmap, samples, pr, data, cpy, cpx, 0, PIXEL_STEPS);
		}
		while(1) pause(); //command exited: keep the final frame until cancelled
		pthread_cleanup_pop(1); //unreachable, balances the push macro
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//like pixel_steps_sparkline, but source is a file path; plots the file's last pr.w lines, redrawn whenever its mtime changes (polled every widget->time seconds)
void* pixel_steps_sparkline_file(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	if(pixel_support<=0) return NULL; //no pixel blitting, render nothing
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags, h, w);
	int cpy, cpx; pixel_cell_dims(widget, &cpy, &cpx);
	int ph = pr.h*cpy, pw = pr.w*cpx;
	struct ncplane* sprite = NULL; //the bitmap's dedicated plane, created on first draw
	PIXEL_ALLOC(block, samples, bitmap, rowbuf, pr.w, ph, pw);
	pthread_cleanup_push(free, block); //free on thread cancel
	if(widget->time>0){
		time_t last_mtime = 0;
		struct stat st;
		while(1){
			if(stat(data->source, &st)==0 && st.st_mtime>last_mtime){
				last_mtime = st.st_mtime;
				plot_read_tail(data->source, samples, pr.w);
				pixel_sparkline_draw(widget, &sprite, rowbuf, bitmap, samples, pr, data, cpy, cpx, 0, PIXEL_STEPS);
			}
			sleep(widget->time);
		}
	}else{
		plot_read_tail(data->source, samples, pr.w);
		pixel_sparkline_draw(widget, &sprite, rowbuf, bitmap, samples, pr, data, cpy, cpx, 0, PIXEL_STEPS);
	}
	pthread_cleanup_pop(1); //frees block, balances the push macro
	return NULL;
}

//true if f is one of the pixel widget functions. used at startup to decide whether to probe the terminal for pixel support
static int is_pixel_widget(void* (*f)(void*)){
	return f==pixel_line_sparkline  || f==pixel_line_sparkline_live  || f==pixel_line_sparkline_file
	    || f==pixel_steps_sparkline || f==pixel_steps_sparkline_live || f==pixel_steps_sparkline_file;
}

#endif
