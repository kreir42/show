//progressbar: a bar that fills in proportion to where the value sits between min and max

//render a horizontal bar of fraction f, repeated on every plot row. buf is scratch of pr.w*3+1 bytes
static void progressbar_draw(struct widget* widget, char* buf, double f, struct plot_region pr, struct plot_data* data){
	long eighths = lround(f * pr.w * 8);
	int full = eighths/8, rem = eighths%8;
	int idx = 0;
	for(int c=0; c<pr.w; c++){ //each cell may differ, so build the row cell by cell
		const char* glyph;
		if(c < full) glyph = PLOT_FULL;
		else if(c==full && rem) glyph = plot_fill_horizontal[rem];
		else glyph = " ";
		size_t len = strlen(glyph);
		memcpy(buf+idx, glyph, len);
		idx += len;
	}
	buf[idx] = '\0';
	for(int r=0; r<pr.h; r++) draw_string(widget, r, pr.left, buf); //same bar on every row
	if(pr.bottom){ //bars already drawn, so buf is free to reuse as the baseline scratch
		plot_draw_x_axis(widget, buf, pr.left, pr.w, pr.h);
		if((data->flags & LABEL_X_AXIS) && pr.bottom>=2){
			//manually add labels
			char lo[PLOT_YLABEL_W*2], hi[PLOT_YLABEL_W*2];
			plot_format_value(data->min, lo, sizeof(lo), PLOT_YLABEL_W);
			plot_format_value(data->max, hi, sizeof(hi), PLOT_YLABEL_W);
			plot_draw_x_labels(widget, pr.left, pr.w, pr.h, lo, hi);
		}
	}
	stage_refresh(widget);
}

//render a vertical bar of fraction f. full_row/blank_row are prebuilt to pr.w. part_row is scratch of pr.w*3+1 bytes
static void vertical_progressbar_draw(struct widget* widget, char* full_row, char* part_row, char* blank_row, double f, struct plot_region pr, struct plot_data* data){
	long eighths = lround(f * pr.h * 8);
	int full = eighths/8, rem = eighths%8;
	if(rem) plot_fill_row(part_row, plot_fill_vertical[rem], pr.w); //only the partial row varies per frame
	for(int r=0; r<pr.h; r++){
		int from_bottom = pr.h-1-r; //0 == bottom row
		char* drawn = (from_bottom < full) ? full_row
		            : (from_bottom==full && rem) ? part_row
		            : blank_row;
		draw_string(widget, r, pr.left, drawn);
	}
	if(pr.left) plot_draw_y_axis(widget, pr.left, pr.h, data->min, data->max, data->flags);
	stage_refresh(widget);
}

//a horizontal bar that fills left-to-right, resampling the source every widget->time seconds
void* progressbar(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags & (PLOT_X_AXIS|LABEL_X_AXIS), h, w); //horizontal bar: only the bottom value axis applies
	size_t rowsize = (size_t)pr.w*3 + 1; //up to 3 UTF-8 bytes per block glyph, +1 for NULL
	char* buf = malloc(rowsize);
	if(!buf) return NULL;
	pthread_cleanup_push(free, buf); //free on thread cancel
	plot_set_color(widget, data->color, data->bg_color);
	if(widget->time>0){
		while(1){
			progressbar_draw(widget, buf, plot_fraction(data), pr, data);
			sleep(widget->time);
		}
	}else{
		progressbar_draw(widget, buf, plot_fraction(data), pr, data);
	}
	pthread_cleanup_pop(1); //frees buf, balances the push macro
	return NULL;
}

//a vertical bar that fills bottom-to-top, resampling the source every widget->time seconds
void* vertical_progressbar(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags & (PLOT_Y_AXIS|LABEL_Y_AXIS), h, w); //vertical bar: only the left value axis applies
	size_t rowsize = (size_t)pr.w*3 + 1; //up to 3 UTF-8 bytes per block glyph, +1 for NULL
	char* buf = malloc(rowsize*3); //three rows: full, partial, blank
	if(!buf) return NULL;
	pthread_cleanup_push(free, buf); //free on thread cancel
	plot_set_color(widget, data->color, data->bg_color);
	char *full_row = buf, *part_row = buf+rowsize, *blank_row = buf+rowsize*2;
	//the full and blank rows never change, so build them once instead of per output row
	plot_fill_row(full_row, PLOT_FULL, pr.w);
	plot_fill_row(blank_row, " ", pr.w);
	if(widget->time>0){
		while(1){
			vertical_progressbar_draw(widget, full_row, part_row, blank_row, plot_fraction(data), pr, data);
			sleep(widget->time);
		}
	}else{
		vertical_progressbar_draw(widget, full_row, part_row, blank_row, plot_fraction(data), pr, data);
	}
	pthread_cleanup_pop(1); //frees buf, balances the push macro
	return NULL;
}

//like progressbar, but the source is launched once and each line it prints updates the bar. widget->time is ignored
void* progressbar_live(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags & (PLOT_X_AXIS|LABEL_X_AXIS), h, w); //horizontal bar: only the bottom value axis applies
	size_t rowsize = (size_t)pr.w*3 + 1; //up to 3 UTF-8 bytes per block glyph, +1 for NULL
	char* buf = malloc(rowsize);
	if(!buf) return NULL;
	pthread_cleanup_push(free, buf); //free on thread cancel
	plot_set_color(widget, data->color, data->bg_color);
	struct plot_live_resources res = { 0, NULL };
	res.fp = plot_spawn(data->source, &res.pid);
	if(res.fp){
		pthread_cleanup_push(plot_live_cleanup, &res); //kill/reap the child on cancel
		progressbar_draw(widget, buf, 0, pr, data); //show an empty bar until the first value arrives
		char line[64];
		while(fgets(line, sizeof(line), res.fp)!=NULL)
			progressbar_draw(widget, buf, plot_clamp(data, strtod(line, NULL)), pr, data);
		while(1) pause(); //command exited: keep the final frame until cancelled
		pthread_cleanup_pop(1); //unreachable, balances the push macro
	}
	pthread_cleanup_pop(1); //frees buf, balances the push macro
	return NULL;
}

//like vertical_progressbar, but the source is launched once and each line it prints updates the bar. widget->time is ignored
void* vertical_progressbar_live(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags & (PLOT_Y_AXIS|LABEL_Y_AXIS), h, w); //vertical bar: only the left value axis applies
	size_t rowsize = (size_t)pr.w*3 + 1; //up to 3 UTF-8 bytes per block glyph, +1 for NULL
	char* buf = malloc(rowsize*3); //three rows: full, partial, blank
	if(!buf) return NULL;
	pthread_cleanup_push(free, buf); //free on thread cancel
	plot_set_color(widget, data->color, data->bg_color);
	char *full_row = buf, *part_row = buf+rowsize, *blank_row = buf+rowsize*2;
	plot_fill_row(full_row, PLOT_FULL, pr.w);
	plot_fill_row(blank_row, " ", pr.w);
	struct plot_live_resources res = { 0, NULL };
	res.fp = plot_spawn(data->source, &res.pid);
	if(res.fp){
		pthread_cleanup_push(plot_live_cleanup, &res); //kill/reap the child on cancel
		vertical_progressbar_draw(widget, full_row, part_row, blank_row, 0, pr, data); //empty bar until first value
		char line[64];
		while(fgets(line, sizeof(line), res.fp)!=NULL)
			vertical_progressbar_draw(widget, full_row, part_row, blank_row, plot_clamp(data, strtod(line, NULL)), pr, data);
		while(1) pause(); //command exited: keep the final frame until cancelled
		pthread_cleanup_pop(1); //unreachable, balances the push macro
	}
	pthread_cleanup_pop(1); //frees buf, balances the push macro
	return NULL;
}

//like progressbar, but source is a file path; the bar shows its last line, redrawn whenever the file's mtime changes (polled every widget->time seconds)
void* progressbar_file(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags & (PLOT_X_AXIS|LABEL_X_AXIS), h, w); //horizontal bar: only the bottom value axis applies
	size_t rowsize = (size_t)pr.w*3 + 1; //up to 3 UTF-8 bytes per block glyph, +1 for NULL
	char* buf = malloc(rowsize);
	if(!buf) return NULL;
	pthread_cleanup_push(free, buf); //free on thread cancel
	plot_set_color(widget, data->color, data->bg_color);
	if(widget->time>0){
		time_t last_mtime = 0;
		struct stat st;
		while(1){
			if(stat(data->source, &st)==0 && st.st_mtime>last_mtime){
				last_mtime = st.st_mtime;
				progressbar_draw(widget, buf, plot_clamp(data, plot_read_last(data->source)), pr, data);
			}
			sleep(widget->time);
		}
	}else{
		progressbar_draw(widget, buf, plot_clamp(data, plot_read_last(data->source)), pr, data);
	}
	pthread_cleanup_pop(1); //frees buf, balances the push macro
	return NULL;
}

//like vertical_progressbar, but source is a file path; the bar shows its last line, redrawn on each mtime change (polled every widget->time seconds)
void* vertical_progressbar_file(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	struct plot_region pr = plot_layout(data->flags & (PLOT_Y_AXIS|LABEL_Y_AXIS), h, w); //vertical bar: only the left value axis applies
	size_t rowsize = (size_t)pr.w*3 + 1; //up to 3 UTF-8 bytes per block glyph, +1 for NULL
	char* buf = malloc(rowsize*3); //three rows: full, partial, blank
	if(!buf) return NULL;
	pthread_cleanup_push(free, buf); //free on thread cancel
	plot_set_color(widget, data->color, data->bg_color);
	char *full_row = buf, *part_row = buf+rowsize, *blank_row = buf+rowsize*2;
	plot_fill_row(full_row, PLOT_FULL, pr.w);
	plot_fill_row(blank_row, " ", pr.w);
	if(widget->time>0){
		time_t last_mtime = 0;
		struct stat st;
		while(1){
			if(stat(data->source, &st)==0 && st.st_mtime>last_mtime){
				last_mtime = st.st_mtime;
				vertical_progressbar_draw(widget, full_row, part_row, blank_row, plot_clamp(data, plot_read_last(data->source)), pr, data);
			}
			sleep(widget->time);
		}
	}else{
		vertical_progressbar_draw(widget, full_row, part_row, blank_row, plot_clamp(data, plot_read_last(data->source)), pr, data);
	}
	pthread_cleanup_pop(1); //frees buf, balances the push macro
	return NULL;
}
