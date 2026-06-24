//this file holds the code common to every plot type; each type lives in its own header under plot/
#include <stdint.h>
#include <math.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <pty.h>

//configuration for every plot widget. supplied via widget->data instead of a string
struct plot_data{
	const char* source;	//shell command printing a number
	double min, max;	//expected value range. defines the empty/full endpoints
	uint32_t color;		//0xRRGGBB main color. 0 = terminal default foreground
	uint32_t bg_color;	//0xRRGGBB color of the unfilled parts. 0 = terminal default background
	int flags;		//plot-specific modifiers
};

//axis flags for plot_data.flags. each LABEL_ flag only takes effect when its matching PLOT_ flag is also set
#define PLOT_X_AXIS	(1<<0)	//draw a horizontal baseline along the bottom
#define LABEL_X_AXIS	(1<<1)	//label the X axis
#define PLOT_Y_AXIS	(1<<2)	//draw a vertical axis line on the left
#define LABEL_Y_AXIS	(1<<3)	//label the Y axis
#define PLOT_YLABEL_W	6	//fixed columns reserved for Y-axis number labels (the axis line takes 1 more)

//axis glyphs
#define PLOT_VLINE	"│"
#define PLOT_HLINE	"─"
#define PLOT_LLCORNER	"└"
#define PLOT_YTICK	"┤"

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

#ifndef USE_NOTCURSES
static __thread short plot_pair = 0; //this thread's cached fg/bg color pair (0 = not yet allocated)
#endif

//turn on the plot's foreground/background color for the cells drawn next. a 0 color/bg leaves the terminal default
static inline void plot_color_on(struct widget* widget, uint32_t color, uint32_t bg_color){
#ifdef USE_NOTCURSES
	if(color==0) ncplane_set_fg_default(widget->window);
	else ncplane_set_fg_rgb8(widget->window, (color>>16)&0xff, (color>>8)&0xff, color&0xff);
	if(bg_color==0) ncplane_set_bg_default(widget->window);
	else ncplane_set_bg_rgb8(widget->window, (bg_color>>16)&0xff, (bg_color>>8)&0xff, bg_color&0xff);
#else
	if(color==0 && bg_color==0) return; //both default: nothing to set
	//each plot thread uses one constant fg/bg, so allocate a single pair once and reuse it
	draw_lock(); //init_pair/wattron touch global+window ncurses state
	if(plot_pair==0){
		plot_pair = plot_next_pair();
		init_pair(plot_pair, plot_color_index(color), plot_color_index(bg_color));
	}
	wattron(widget->window, COLOR_PAIR(plot_pair));
	draw_unlock();
#endif
}

//revert to the terminal default colors, so the axes and labels aren't tinted by the plot color
static inline void plot_color_off(struct widget* widget, uint32_t color, uint32_t bg_color){
#ifdef USE_NOTCURSES
	(void)color; (void)bg_color;
	ncplane_set_fg_default(widget->window);
	ncplane_set_bg_default(widget->window);
#else
	if(color==0 && bg_color==0) return; //color was never turned on
	draw_lock();
	if(plot_pair!=0) wattroff(widget->window, COLOR_PAIR(plot_pair)); //clears just the color bits, leaving any bold/italic
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

//the plot area after the axes carve out their margins: rows [0,h), columns [left, left+w)
struct plot_region{ int left, bottom, w, h; };

//resolve the plot area for a widget of total h x w. flags should already be masked to the axes this widget supports. an axis is dropped whole if its margin wouldn't leave room
static struct plot_region plot_layout(int flags, int h, int w){
	int left = (flags & PLOT_Y_AXIS) ? ((flags & LABEL_Y_AXIS) ? PLOT_YLABEL_W+1 : 1) : 0;
	int bottom = (flags & PLOT_X_AXIS) ? ((flags & LABEL_X_AXIS) ? 2 : 1) : 0;
	if(w-left < 1) left = 0;
	if(h-bottom < 1) bottom = 0;
	return (struct plot_region){ left, bottom, w-left, h-bottom };
}

//format v into out using at most "width" visible characters: the most significant digits that fit, falling back to scientific notation, then a hard truncation as a last resort
static void plot_format_value(double v, char* out, size_t cap, int width){
	for(int prec=width; prec>=1; prec--){
		snprintf(out, cap, "%.*g", prec, v); //%g switches to scientific on its own when the plain form is too wide
		if((int)strlen(out) <= width) return;
	}
	if((int)strlen(out) > width) out[width] = '\0';
}

//format a duration as "-<n><unit>" using the largest whole unit (s/m/h/d)
static void plot_format_span(long secs, char* out, size_t cap){
	if(secs < 60)         snprintf(out, cap, "-%lds", secs);
	else if(secs < 3600)  snprintf(out, cap, "-%ldm", secs/60);
	else if(secs < 86400) snprintf(out, cap, "-%ldh", secs/3600);
	else                  snprintf(out, cap, "-%ldd", secs/86400);
}

//draw the left Y-axis line over rows [0,plot_h). with LABEL_Y_AXIS, label hi at the top row and lo at the bottom row. the axis line sits at column left-1, labels right-aligned in the columns before it
static void plot_draw_y_axis(struct widget* widget, int left, int plot_h, double lo, double hi, int flags){
	int labeled = (flags & LABEL_Y_AXIS) && left > 1;
	for(int r=0; r<plot_h; r++){
		int tick = labeled && (r==0 || r==plot_h-1);
		if(labeled){
			char field[PLOT_YLABEL_W*2]; field[0] = '\0';
			if(r==0) plot_format_value(hi, field, sizeof(field), PLOT_YLABEL_W);
			else if(r==plot_h-1) plot_format_value(lo, field, sizeof(field), PLOT_YLABEL_W);
			char padded[PLOT_YLABEL_W+1];
			snprintf(padded, sizeof(padded), "%*s", PLOT_YLABEL_W, field); //right-align next to the axis
			draw_string(widget, r, 0, padded);
		}
		draw_string(widget, r, left-1, tick ? PLOT_YTICK : PLOT_VLINE);
	}
}

//draw the X-axis baseline at row plot_h spanning [left, left+plot_w), plus the corner where it meets a Y axis. scratch must hold at least plot_w*3+1 bytes
static void plot_draw_x_axis(struct widget* widget, char* scratch, int left, int plot_w, int plot_h){
	if(left>0) draw_string(widget, plot_h, left-1, PLOT_LLCORNER);
	plot_fill_row(scratch, PLOT_HLINE, plot_w);
	draw_string(widget, plot_h, left, scratch);
}

//draw a left-aligned and a right-aligned ASCII label on the X-axis label row (plot_h+1), within [left, left+plot_w). the right label is skipped if it would collide with the left one
static void plot_draw_x_labels(struct widget* widget, int left, int plot_w, int plot_h, const char* lstr, const char* rstr){
	int row = plot_h+1;
	int llen = lstr ? (int)strlen(lstr) : 0;
	if(llen) draw_string(widget, row, left, lstr);
	if(rstr && rstr[0]){
		int rx = left + plot_w - (int)strlen(rstr);
		if(rx > left + llen) draw_string(widget, row, rx, rstr);
	}
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

//parse a contiguous block of complete lines, keeping the last "want" numeric values in out (newest last), right-aligned with NAN padding. returns how many numeric lines were found
static int plot_parse_region(const char* region, size_t len, double* out, int want){
	for(int i=0; i<want; i++) out[i] = NAN;
	int count = 0;
	size_t i = 0;
	while(i<len){
		size_t j = i;
		while(j<len && region[j]!='\n') j++; //line is region[i..j)
		size_t linelen = j-i;
		if(linelen>0){
			char tmp[64]; //read at most 63 bytes per line
			size_t cpy = linelen < sizeof(tmp)-1 ? linelen : sizeof(tmp)-1;
			memcpy(tmp, region+i, cpy); tmp[cpy] = '\0';
			char* end;
			double v = strtod(tmp, &end);
			if(end!=tmp){ //skip non-numeric/blank lines
				if(count<want) out[count++] = v;
				else { memmove(out, out+1, (size_t)(want-1)*sizeof(double)); out[want-1] = v; } //ring: drop oldest
			}
		}
		i = j+1; //start of next line
	}
	if(count>0 && count<want){ //right-align the parsed values, NAN-padding the unused leading columns
		memmove(out+(want-count), out, (size_t)count*sizeof(double));
		for(int k=0; k<want-count; k++) out[k] = NAN;
	}
	return count;
}

//fill out[0..want-1] with the file's last "want" numeric lines (newest last), right-aligned with NAN padding when the file is shorter
//reads the file backward a 4KB block at a time and stops once enough numeric lines are in hand
static void plot_read_tail(const char* path, double* out, int want){
	for(int i=0; i<want; i++) out[i] = NAN;
	if(want<=0) return; //TODO can that happen?
	FILE* fp = fopen(path, "rb");
	if(fp==NULL) return;
	if(fseeko(fp, 0, SEEK_END)!=0){ fclose(fp); return; } //jump to end
	off_t pos = ftello(fp); //pos = filesize
	if(pos<0){ fclose(fp); return; }
	char* buf = NULL; size_t buflen = 0, bufcap = 0;
	const size_t CHUNK = 4096; //read in 4KB chunks
	while(pos>0){
		size_t r = pos < (off_t)CHUNK ? (size_t)pos : CHUNK; //read 4KB chunk, or whatever's left
		if(buflen+r > bufcap){
			char* nb = realloc(buf, buflen+r); //grow the buffer
			if(nb==NULL) break; //out keeps whatever the previous parse found
			buf = nb; bufcap = buflen+r;
		}
		memmove(buf+r, buf, buflen); //make room, keeping bytes in file order (oldest..newest)
		if(fseeko(fp, pos-(off_t)r, SEEK_SET)!=0 || fread(buf, 1, r, fp)!=r) break; //read to buffer
		buflen += r; pos -= (off_t)r;
		//only lines after the first newline are whole, unless we've reached the start of the file
		const char* region = buf; size_t regionlen = buflen;
		if(pos>0){
			char* nl = memchr(buf, '\n', buflen);
			if(nl==NULL) continue; //no line boundary yet: read another block
			region = nl+1; regionlen = buflen - (size_t)(nl+1 - buf);
		}
		//parse and see if we have enough lines
		if(plot_parse_region(region, regionlen, out, want) >= want) break;
	}
	free(buf);
	fclose(fp);
}

//read the last numeric line of a file, or NAN if it is missing or has none
static double plot_read_last(const char* path){
	double v;
	plot_read_tail(path, &v, 1);
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
		reset_child_sigmask(); //don't leak our blocked SIGWINCH into the spawned command
		execl("/bin/sh", "sh", "-c", source, NULL);
		_exit(1);
	}
	FILE* fp = fdopen(master, "r");
	if(fp==NULL){
		close(master);
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		return NULL; //*pid_out left unset
	}
	*pid_out = pid;
	return fp;
}

struct plot_live_resources{ pid_t pid; FILE* fp; };
//kill and reap the child and close the stream on cancel or normal exit
static void plot_live_cleanup(void* arg){
	struct plot_live_resources* r = arg;
	kill(r->pid, SIGKILL);
	if(r->fp) fclose(r->fp);
	waitpid(r->pid, NULL, 0);
}

//per-thread scratch shared by the time-series plots (bar_sparkline, stairs_sparkline): a rolling buffer of "count" raw samples, a per-column int array, and a row string, carved from one malloc (doubles first for alignment) and freed on cancel. "count" is the column count (pr.w); samples start as NAN
#define PLOT_HISTORY_ALLOC(block, samples, ints, rowbuf, count) \
	char* block = malloc((size_t)(count)*sizeof(double) + (size_t)(count)*sizeof(int) + ((size_t)(count)*3 + 1)); \
	if(!block) return NULL; \
	double* samples = (double*)block; \
	int* ints = (int*)(samples + (count)); \
	char* rowbuf = (char*)(ints + (count)); \
	for(int i=0; i<(count); i++) samples[i] = NAN

//push one sample onto the right of a "count" value history, dropping the oldest on the left
static inline void plot_history_push(double* samples, int count, double v){
	memmove(samples, samples+1, (size_t)(count-1)*sizeof(double));
	samples[count-1] = v;
}

#include "plot/progressbar.h"
#include "plot/bar_sparkline.h"
#include "plot/stairs_sparkline.h"
#include "plot/braille_sparkline.h"
#include "plot/pixel_sparkline.h"
