#include <stdint.h>
#include <math.h>
#include <signal.h>
#include <sys/wait.h>
#include <pty.h>

struct plot_data{
	const char* source;	//shell command printing a number
	double min, max;	//expected value range. defines the bar's empty/full endpoints
	uint32_t color;		//0xRRGGBB main color. 0 = terminal default foreground
	uint32_t bg_color;	//0xRRGGBB color of the unfilled parts. 0 = terminal default background
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
//approximate a 0xRRGGBB color onto the xterm-256 palette, or -1 (terminal default) when color==0
static short plot_color_index(uint32_t color){
	if(color==0) return -1;
	int r = (color>>16)&0xff, g = (color>>8)&0xff, b = color&0xff;
	return 16 + ((r*5+127)/255)*36 + ((g*5+127)/255)*6 + ((b*5+127)/255);
}
#endif

//set the widget's foreground/background colors before drawing. a 0 color leaves the terminal default
static inline void plot_set_color(struct widget* widget, uint32_t color, uint32_t bg_color){
#ifdef USE_NOTCURSES
	if(color==0) ncplane_set_fg_default(widget->window);
	else ncplane_set_fg_rgb8(widget->window, (color>>16)&0xff, (color>>8)&0xff, color&0xff);
	if(bg_color==0) ncplane_set_bg_default(widget->window);
	else ncplane_set_bg_rgb8(widget->window, (bg_color>>16)&0xff, (bg_color>>8)&0xff, bg_color&0xff);
#else
	if(color==0 && bg_color==0) return; //both default: nothing to set
	//each plot thread uses one constant fg/bg, so allocate a single pair once and reuse it
	static __thread short pair = 0;
	draw_lock(); //init_pair/wattron touch global+window ncurses state
	if(pair==0){
		pair = plot_next_pair();
		init_pair(pair, plot_color_index(color), plot_color_index(bg_color));
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

//map a raw value to the fill fraction in [0,1]. returns 0 if the value is NaN
static inline double progressbar_clamp(struct plot_data* data, double val){
	double span = data->max - data->min; //we assume span>0
	double f = !isnan(val) ? (val - data->min)/span : 0;
	if(f<0) f = 0; else if(f>1) f = 1; //clamp to [0,1] range
	return f;
}

//sample the source once and return the fill fraction in [0,1]
static inline double progressbar_fraction(struct plot_data* data){
	return progressbar_clamp(data, progressbar_sample(data->source));
}

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

//launch source once and return a stdio stream over its output, setting *pid_out. returns NULL on failure
//uses a pty so the child's stdout is a tty and libc line-buffers it: every printed line flushes immediately, redrawing the bar even for an infrequent source
static FILE* progressbar_spawn(const char* source, pid_t* pid_out){
	int master;
	struct winsize ws = { .ws_row = 24, .ws_col = 80 };
	pid_t pid = forkpty(&master, NULL, NULL, &ws);
	if(pid<0) return NULL;
	if(pid==0){ //child
		execl("/bin/sh", "sh", "-c", source, NULL);
		_exit(1);
	}
	*pid_out = pid;
	return fdopen(master, "r");
}

struct progressbar_live_resources{ pid_t pid; FILE* fp; };
//kill and reap the child and close the stream on cancel or normal exit
static void progressbar_live_cleanup(void* arg){
	struct progressbar_live_resources* r = arg;
	kill(r->pid, SIGKILL);
	if(r->fp) fclose(r->fp);
	waitpid(r->pid, NULL, 0);
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
			progressbar_draw(widget, buf, progressbar_fraction(data), h, w);
			sleep(widget->time);
		}
	}else{
		progressbar_draw(widget, buf, progressbar_fraction(data), h, w);
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
			vertical_progressbar_draw(widget, full_row, part_row, blank_row, progressbar_fraction(data), h, w);
			sleep(widget->time);
		}
	}else{
		vertical_progressbar_draw(widget, full_row, part_row, blank_row, progressbar_fraction(data), h, w);
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
	struct progressbar_live_resources res = { 0, NULL };
	res.fp = progressbar_spawn(data->source, &res.pid);
	if(res.fp){
		pthread_cleanup_push(progressbar_live_cleanup, &res); //kill/reap the child on cancel
		progressbar_draw(widget, buf, 0, h, w); //show an empty bar until the first value arrives
		char line[64];
		while(fgets(line, sizeof(line), res.fp)!=NULL)
			progressbar_draw(widget, buf, progressbar_clamp(data, strtod(line, NULL)), h, w);
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
	struct progressbar_live_resources res = { 0, NULL };
	res.fp = progressbar_spawn(data->source, &res.pid);
	if(res.fp){
		pthread_cleanup_push(progressbar_live_cleanup, &res); //kill/reap the child on cancel
		vertical_progressbar_draw(widget, full_row, part_row, blank_row, 0, h, w); //empty bar until first value
		char line[64];
		while(fgets(line, sizeof(line), res.fp)!=NULL)
			vertical_progressbar_draw(widget, full_row, part_row, blank_row, progressbar_clamp(data, strtod(line, NULL)), h, w);
		while(1) pause(); //command exited: keep the final frame until cancelled
		pthread_cleanup_pop(1); //unreachable, balances the push macro
	}
	pthread_cleanup_pop(1); //frees buf, balances the push macro
	return NULL;
}
