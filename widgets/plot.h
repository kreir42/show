#include <stdint.h>
#include <math.h>

struct plot_data{
	const char* source;	//shell command printing a number
	double min, max;	//expected value range. defines the bar's empty/full endpoints
	uint32_t color;		//0xRRGGBB main color. 0 = terminal default foreground
	int flags;		//plot-specific modifiers
};

static const char* plot_fill_horizontal[8] = { "", "▏", "▎", "▍", "▌", "▋", "▊", "▉" };
static const char* plot_fill_vertical[8]   = { "", "▁", "▂", "▃", "▄", "▅", "▆", "▇" };
#define PLOT_FULL "█"

#ifndef USE_NOTCURSES
//hand out distinct color-pair numbers across plot threads
static short plot_next_pair(void){
	static short next = 1;
	if(next >= COLOR_PAIRS) return COLOR_PAIRS-1; //out of pairs: reuse the last one
	return next++;
}
#endif

//set the widget's foreground color before drawing. color==0 leaves the terminal default
static inline void plot_set_color(struct widget* widget, uint32_t color){
	int r = (color>>16)&0xff, g = (color>>8)&0xff, b = color&0xff;
#ifdef USE_NOTCURSES
	if(color==0) ncplane_set_fg_default(widget->window);
	else ncplane_set_fg_rgb8(widget->window, r, g, b);
#else
	if(color==0) return; //leave default fg
	//each plot thread uses one constant color, so allocate a single pair once and reuse it
	static __thread short pair = 0;
	draw_lock(); //init_pair/wattron touch global+window ncurses state
	if(pair==0){
		short idx = 16 + ((r*5+127)/255)*36 + ((g*5+127)/255)*6 + ((b*5+127)/255); //RGB -> xterm-256
		pair = plot_next_pair();
		init_pair(pair, idx, -1);
	}
	wattron(widget->window, COLOR_PAIR(pair));
	draw_unlock();
#endif
}

//sample the source command once and return its first line parsed as a number, or NAN on failure
static inline double progressbar_sample(const char* source){
	FILE* fp = popen(source, "r");
	if(fp==NULL) return NAN;
	char buf[64];
	double v = NAN;
	if(fgets(buf, sizeof(buf), fp)!=NULL) v = strtod(buf, NULL);
	pclose(fp);
	return v;
}

//fill dst with n copies of the UTF-8 glyph and NUL-terminate
static void plot_fill_row(char* dst, const char* glyph, int n){
	size_t len = strlen(glyph);
	int idx = 0;
	for(int c=0; c<n; c++){ memcpy(dst+idx, glyph, len); idx += len; }
	dst[idx] = '\0';
}

//sample the source and return the fill fraction in [0,1]. returns 0 if the value is NaN
static inline double progressbar_fraction(struct plot_data* data){
	double val = progressbar_sample(data->source);
	double span = data->max - data->min; //we assume span>0
	double f = !isnan(val) ? (val - data->min)/span : 0;
	if(f<0) f = 0; else if(f>1) f = 1; //clamp to [0,1] range
	return f;
}

//a horizontal bar that fills left-to-right
void* progressbar(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	size_t rowsize = (size_t)w*3 + 1; //up to 3 UTF-8 bytes per block glyph, +1 for NUL
	char* buf = malloc(rowsize);
	if(!buf) return NULL;
	pthread_cleanup_push(free, buf); //free on thread cancel
	plot_set_color(widget, data->color);
	do{
		double f = progressbar_fraction(data);
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
		if(widget->time>0) sleep(widget->time);
	}while(widget->time>0);
	pthread_cleanup_pop(1); //frees buf, balances the push macro
	return NULL;
}

//a vertical bar that fills bottom-to-top in proportion to where the sampled value sits between min and max
void* vertical_progressbar(void* input){
	struct widget* widget = input;
	struct plot_data* data = widget->data;
	int h, w;
	get_size(widget, &h, &w);
	size_t rowsize = (size_t)w*3 + 1; //up to 3 UTF-8 bytes per block glyph, +1 for NUL
	char* buf = malloc(rowsize*3); //three rows: full, partial, blank
	if(!buf) return NULL;
	pthread_cleanup_push(free, buf); //free on thread cancel
	plot_set_color(widget, data->color);
	char *full_row = buf, *part_row = buf+rowsize, *blank_row = buf+rowsize*2;
	//the full and blank rows never change, so build them once instead of per output row
	plot_fill_row(full_row, PLOT_FULL, w);
	plot_fill_row(blank_row, " ", w);
	do{
		double f = progressbar_fraction(data);
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
		if(widget->time>0) sleep(widget->time);
	}while(widget->time>0);
	pthread_cleanup_pop(1); //frees buf, balances the push macro
	return NULL;
}
