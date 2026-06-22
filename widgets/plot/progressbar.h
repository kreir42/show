//progressbar: a bar that fills in proportion to where the value sits between min and max

//render a horizontal bar of fraction f, repeated on every row. buf is scratch of rowsize bytes
static void progressbar_draw(struct widget* widget, char* buf, double f, int h, int w){
	long eighths = lround(f * w * 8);
	int full = eighths/8, rem = eighths%8;
	int idx = 0;
	for(int c=0; c<w; c++){ //each cell may differ, so build the row cell by cell
		const char* glyph;
		if(c < full) glyph = PLOT_FULL;
		else if(c==full && rem) glyph = plot_fill_horizontal[rem];
		else glyph = " ";
		size_t len = strlen(glyph);
		memcpy(buf+idx, glyph, len);
		idx += len;
	}
	buf[idx] = '\0';
	for(int r=0; r<h; r++) draw_string(widget, r, 0, buf); //same bar on every row
	stage_refresh(widget);
}

//render a vertical bar of fraction f. full_row/blank_row are prebuilt; part_row is scratch of rowsize bytes
static void vertical_progressbar_draw(struct widget* widget, char* full_row, char* part_row, char* blank_row, double f, int h, int w){
	long eighths = lround(f * h * 8);
	int full = eighths/8, rem = eighths%8;
	if(rem) plot_fill_row(part_row, plot_fill_vertical[rem], w); //only the partial row varies per frame
	for(int r=0; r<h; r++){
		int from_bottom = h-1-r; //0 == bottom row
		char* drawn = (from_bottom < full) ? full_row
		            : (from_bottom==full && rem) ? part_row
		            : blank_row;
		draw_string(widget, r, 0, drawn);
	}
	stage_refresh(widget);
}

//a horizontal bar that fills left-to-right, resampling the source every widget->time seconds
void* progressbar(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	size_t rowsize = (size_t)w*3 + 1; //up to 3 UTF-8 bytes per block glyph, +1 for NUL
	char* buf = malloc(rowsize);
	if(!buf) return NULL;
	pthread_cleanup_push(free, buf); //free on thread cancel
	plot_set_color(widget, data->color, data->bg_color);
	if(widget->time>0){
		while(1){
			progressbar_draw(widget, buf, plot_fraction(data), h, w);
			sleep(widget->time);
		}
	}else{
		progressbar_draw(widget, buf, plot_fraction(data), h, w);
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
	size_t rowsize = (size_t)w*3 + 1; //up to 3 UTF-8 bytes per block glyph, +1 for NUL
	char* buf = malloc(rowsize*3); //three rows: full, partial, blank
	if(!buf) return NULL;
	pthread_cleanup_push(free, buf); //free on thread cancel
	plot_set_color(widget, data->color, data->bg_color);
	char *full_row = buf, *part_row = buf+rowsize, *blank_row = buf+rowsize*2;
	//the full and blank rows never change, so build them once instead of per output row
	plot_fill_row(full_row, PLOT_FULL, w);
	plot_fill_row(blank_row, " ", w);
	if(widget->time>0){
		while(1){
			vertical_progressbar_draw(widget, full_row, part_row, blank_row, plot_fraction(data), h, w);
			sleep(widget->time);
		}
	}else{
		vertical_progressbar_draw(widget, full_row, part_row, blank_row, plot_fraction(data), h, w);
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
	size_t rowsize = (size_t)w*3 + 1; //up to 3 UTF-8 bytes per block glyph, +1 for NUL
	char* buf = malloc(rowsize);
	if(!buf) return NULL;
	pthread_cleanup_push(free, buf); //free on thread cancel
	plot_set_color(widget, data->color, data->bg_color);
	struct plot_live_resources res = { 0, NULL };
	res.fp = plot_spawn(data->source, &res.pid);
	if(res.fp){
		pthread_cleanup_push(plot_live_cleanup, &res); //kill/reap the child on cancel
		progressbar_draw(widget, buf, 0, h, w); //show an empty bar until the first value arrives
		char line[64];
		while(fgets(line, sizeof(line), res.fp)!=NULL)
			progressbar_draw(widget, buf, plot_clamp(data, strtod(line, NULL)), h, w);
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
	size_t rowsize = (size_t)w*3 + 1; //up to 3 UTF-8 bytes per block glyph, +1 for NUL
	char* buf = malloc(rowsize*3); //three rows: full, partial, blank
	if(!buf) return NULL;
	pthread_cleanup_push(free, buf); //free on thread cancel
	plot_set_color(widget, data->color, data->bg_color);
	char *full_row = buf, *part_row = buf+rowsize, *blank_row = buf+rowsize*2;
	plot_fill_row(full_row, PLOT_FULL, w);
	plot_fill_row(blank_row, " ", w);
	struct plot_live_resources res = { 0, NULL };
	res.fp = plot_spawn(data->source, &res.pid);
	if(res.fp){
		pthread_cleanup_push(plot_live_cleanup, &res); //kill/reap the child on cancel
		vertical_progressbar_draw(widget, full_row, part_row, blank_row, 0, h, w); //empty bar until first value
		char line[64];
		while(fgets(line, sizeof(line), res.fp)!=NULL)
			vertical_progressbar_draw(widget, full_row, part_row, blank_row, plot_clamp(data, strtod(line, NULL)), h, w);
		while(1) pause(); //command exited: keep the final frame until cancelled
		pthread_cleanup_pop(1); //unreachable, balances the push macro
	}
	pthread_cleanup_pop(1); //frees buf, balances the push macro
	return NULL;
}
