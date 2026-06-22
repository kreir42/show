//this file holds the code common to every plot type; each type lives in its own header under plot/
#include <stdint.h>
#include <math.h>
#include <signal.h>
#include <sys/wait.h>
#include <pty.h>

//configuration for every plot widget. supplied via widget->data instead of a string
struct plot_data{
	const char* source;	//shell command printing a number
	double min, max;	//expected value range. defines the empty/full endpoints
	uint32_t color;		//0xRRGGBB main color. 0 = terminal default foreground
	uint32_t bg_color;	//0xRRGGBB color of the unfilled parts. 0 = terminal default background
	int flags;		//plot-specific modifiers
};

//block-fill glyphs shared by plot types: partial cells indexed by eighths 1..7, plus the full cell
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

//fill dst with n copies of the UTF-8 glyph and NUL-terminate
static void plot_fill_row(char* dst, const char* glyph, int n){
	size_t len = strlen(glyph);
	int idx = 0;
	for(int c=0; c<n; c++){ memcpy(dst+idx, glyph, len); idx += len; }
	dst[idx] = '\0';
}

//sample the source command once and return its first line parsed as a number, or NAN on failure
static inline double plot_sample(const char* source){
	FILE* fp = popen(source, "r");
	if(fp==NULL) return NAN;
	char buf[64];
	double v = NAN;
	if(fgets(buf, sizeof(buf), fp)!=NULL) v = strtod(buf, NULL);
	pclose(fp);
	return v;
}

//map a raw value to the fill fraction in [0,1]. returns 0 if the value is NaN
static inline double plot_clamp(struct plot_data* data, double val){
	double span = data->max - data->min; //we assume span>0
	double f = !isnan(val) ? (val - data->min)/span : 0;
	if(f<0) f = 0; else if(f>1) f = 1; //clamp to [0,1] range
	return f;
}

//sample the source once and return the fill fraction in [0,1]
static inline double plot_fraction(struct plot_data* data){
	return plot_clamp(data, plot_sample(data->source));
}

//launch source once and return a stdio stream over its output, setting *pid_out. returns NULL on failure
//uses a pty so the child's stdout is a tty and libc line-buffers it: every printed line flushes immediately, redrawing even for an infrequent source
static FILE* plot_spawn(const char* source, pid_t* pid_out){
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

struct plot_live_resources{ pid_t pid; FILE* fp; };
//kill and reap the child and close the stream on cancel or normal exit
static void plot_live_cleanup(void* arg){
	struct plot_live_resources* r = arg;
	kill(r->pid, SIGKILL);
	if(r->fp) fclose(r->fp);
	waitpid(r->pid, NULL, 0);
}

#include "plot/progressbar.h"
#include "plot/sparkline.h"
