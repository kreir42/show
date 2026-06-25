//substitute the size placeholders in a command template, returning a freshly malloc'd command string (caller frees), or NULL on allocation failure
//{{w}}/{{h}}: widget size in cells. {{pw}}/{{ph}}: widget size in pixels
static char* substitute_placeholders(const char* tmpl, int w, int h, int pw, int ph) {
	char* out = NULL;
	size_t outlen = 0;
	FILE* ms = open_memstream(&out, &outlen);
	if(!ms) return NULL;
	for(const char* p = tmpl; *p; ){
		if(p[0] == '{' && p[1] == '{'){
			if(strncmp(p, "{{pw}}", 6) == 0){ fprintf(ms, "%d", pw); p += 6; continue; }
			if(strncmp(p, "{{ph}}", 6) == 0){ fprintf(ms, "%d", ph); p += 6; continue; }
			if(strncmp(p, "{{w}}", 5) == 0){ fprintf(ms, "%d", w); p += 5; continue; }
			if(strncmp(p, "{{h}}", 5) == 0){ fprintf(ms, "%d", h); p += 5; continue; }
		}
		fputc(*p++, ms);
	}
	if(fclose(ms) != 0){ free(out); return NULL; }
	return out;
}

//shows the result of a shell command
static inline void draw_text_external_command(struct widget* widget, int h, int w, char* str, const char* cmd) {
	FILE* fp = popen(cmd, "r");
	if(fp == NULL) return; //popen failed
	draw_lock();
#ifdef USE_NOTCURSES
	ncplane_erase(widget->window);
#else
	werase(widget->window);
#endif
	draw_unlock();
	for(unsigned short i=0; i<h; i++){
		if(fgets(str, w, fp)==NULL) break;	//exit early if command output ends
		//if last char is a newline, remove
		size_t len = strlen(str);
		if(len>0 && str[len-1]=='\n'){
			str[len-1] = '\0';
			if(len==1){	//first and only char was a newline
				i--;
				continue;
			}
		}
		draw_string(widget, i, 0, str);
	}
	pclose(fp);
	stage_refresh(widget);
}

void* text_external_command(void* input){
	struct widget* widget = input;
	int h, w;
	get_size(widget, &h, &w);
	w++;	//+1 for the NULL terminator
	char* str = malloc(w*sizeof(char));
	if(!str) return NULL; //check for failed malloc
	int t = widget->time;
	if (t <= 0) {
		draw_text_external_command(widget, h, w, str, widget->data);
		free(str);
	} else {
		pthread_cleanup_push(free, str); //free str on thread cancel
		while(1){
			draw_text_external_command(widget, h, w, str, widget->data);
			sleep(t);
		}
		pthread_cleanup_pop(1);	//unreachable, balances pthread_cleanup_push macro
	}
	return NULL;
}

//like text_external_command, but the command is a template whose size placeholders are substituted with this widget's size before each run
void* dynamic_text_external_command(void* input){
	struct widget* widget = input;
	int h, w;
	get_size(widget, &h, &w);
	int ph, pw;
	get_pixel_size(widget, &ph, &pw);
	char* cmd = substitute_placeholders(widget->data, w, h, pw, ph);
	if(!cmd) return NULL;
	w++;	//+1 for the NULL terminator
	char* str = malloc(w*sizeof(char));
	if(!str){ free(cmd); return NULL; } //check for failed malloc
	int t = widget->time;
	if (t <= 0) {
		draw_text_external_command(widget, h, w, str, cmd);
		free(str);
		free(cmd);
	} else {
		pthread_cleanup_push(free, cmd); //free cmd on thread cancel
		pthread_cleanup_push(free, str); //free str on thread cancel
		while(1){
			draw_text_external_command(widget, h, w, str, cmd);
			sleep(t);
		}
		pthread_cleanup_pop(1);	//unreachable, balances pthread_cleanup_push macro
		pthread_cleanup_pop(1);	//unreachable, balances pthread_cleanup_push macro
	}
	return NULL;
}

#include <pty.h>
#include <vterm.h>
#include <unistd.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <signal.h>

struct external_command_resources{
	int master;
	pid_t pid;
	VTerm* vt;
};
//handler to clear resources on cancel
static void ec_cleanup(void* arg) {
	struct external_command_resources* r = arg;
	kill(r->pid, SIGKILL);
	waitpid(r->pid, NULL, 0);
	close(r->master);
	if(r->vt) vterm_free(r->vt);
}

//render the current vterm screen contents to the widget's window
static inline void render_vterm_screen(struct widget* widget, VTermScreen* vts, int h, int w) {
#ifndef USE_NOTCURSES
	//if ncurses, setup cache for color pairs. per-thread and persists across calls so repeated renders reuse pairs instead of exhausting COLOR_PAIRS
	static __thread short next_pair = 1;
	static __thread short pair_map[256][256]; //cache for up to 256x256 combinations
#endif
	draw_lock(); //serialize the direct backend drawing below against the render loop
#ifdef USE_NOTCURSES
	ncplane_erase(widget->window);
#else
	werase(widget->window);
#endif

	VTermPos pos;
	for (pos.row = 0; pos.row < h; pos.row++) { //iterate over rows
		int last_col = -1;
		for (pos.col = w - 1; pos.col >= 0; --pos.col) { //find number of columns for row
			VTermScreenCell cell;
			vterm_screen_get_cell(vts, pos, &cell);
			if (cell.chars[0] != 0 && cell.chars[0] != ' ' && cell.chars[0] != (uint32_t)-1) {
				last_col = pos.col;
				break;
			}
		}
		if (last_col == -1) continue; //skip empty rows

		for (pos.col = 0; pos.col <= last_col; pos.col++) { //iterate over columns in row
#ifdef USE_NOTCURSES
			VTermScreenCell cell;
			vterm_screen_get_cell(vts, pos, &cell);
			if (cell.chars[0] == 0 || cell.chars[0] == (uint32_t)-1) continue; //skip empty
			char utf8[VTERM_MAX_CHARS_PER_CELL * MB_CUR_MAX + 1];
			memset(utf8, 0, sizeof(utf8));
			int i = 0;
			int len = 0;
			while (i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0) {
				mbstate_t state; memset(&state, 0, sizeof(state)); int l = wcrtomb(utf8 + len, cell.chars[i], &state); //wide character to multi-byte
				if (l > 0) len += l;
				i++;
			}
			if (i == 0) continue;
			utf8[len] = '\0';

			nccell nc_cell;
			nccell_init(&nc_cell);
			if (nccell_load(widget->window, &nc_cell, utf8) < 0) continue; //skip if failed
			switch(cell.fg.type){ //foreground color
				case VTERM_COLOR_RGB:
					nccell_set_fg_rgb8(&nc_cell, cell.fg.rgb.red, cell.fg.rgb.green, cell.fg.rgb.blue);
					break;
				case VTERM_COLOR_INDEXED:
					nccell_set_fg_palindex(&nc_cell, cell.fg.indexed.idx);
					break;
				default:
					nccell_set_fg_default(&nc_cell);
					break;
			}
			switch(cell.bg.type){ //background color
				case VTERM_COLOR_RGB:
					nccell_set_bg_rgb8(&nc_cell, cell.bg.rgb.red, cell.bg.rgb.green, cell.bg.rgb.blue);
					break;
				case VTERM_COLOR_INDEXED:
					nccell_set_bg_palindex(&nc_cell, cell.bg.indexed.idx);
					break;
				default:
					nccell_set_bg_default(&nc_cell);
					break;
			}
			if (cell.attrs.bold) nccell_on_styles(&nc_cell, NCSTYLE_BOLD);
			if (cell.attrs.underline) nccell_on_styles(&nc_cell, NCSTYLE_UNDERLINE);
			if (cell.attrs.italic) nccell_on_styles(&nc_cell, NCSTYLE_ITALIC);
			//if (cell.attrs.blink); //TODO? blink requires ncplane_pulse() on the entire plane, not easily done per-cell
			if (cell.attrs.reverse) {
				//if using default colors, we must explicitly set them to invert them visually
				if (cell.fg.type == VTERM_COLOR_DEFAULT_FG && cell.bg.type == VTERM_COLOR_DEFAULT_BG) {
					nccell_set_fg_rgb8(&nc_cell, 0, 0, 0);       //black text
					nccell_set_bg_rgb8(&nc_cell, 255, 255, 255); //white background
				} else nc_cell.channels = ncchannels_reverse(nc_cell.channels);
			}
			if (cell.attrs.conceal) nccell_set_fg_alpha(&nc_cell, NCALPHA_TRANSPARENT);
			if (cell.attrs.strike) nccell_on_styles(&nc_cell, NCSTYLE_STRUCK);
			//if (cell.attrs.font); //TODO? alternative fonts
			//if (cell.attrs.dwl); if (cell.attrs.dhl); //TODO? double-width/double-height lines
			//if (cell.attrs.small); //TODO? small font
			//if (cell.attrs.baseline); //TODO? baseline shift
			ncplane_putc_yx(widget->window, pos.row, pos.col, &nc_cell);
			nccell_release(widget->window, &nc_cell);
#else
			VTermScreenCell cell;
			vterm_screen_get_cell(vts, pos, &cell);
			//toggle attributes based on the virtual terminal cell
			attr_t on_attrs = 0;
			attr_t off_attrs = 0;
			(cell.attrs.bold)      ? (on_attrs |= A_BOLD)      : (off_attrs |= A_BOLD);
			(cell.attrs.underline) ? (on_attrs |= A_UNDERLINE) : (off_attrs |= A_UNDERLINE);
#ifdef A_ITALIC //italic not supported in all versions of ncurses
			(cell.attrs.italic)    ? (on_attrs |= A_ITALIC)    : (off_attrs |= A_ITALIC);
#endif
			(cell.attrs.blink)     ? (on_attrs |= A_BLINK)     : (off_attrs |= A_BLINK);
			(cell.attrs.reverse)   ? (on_attrs |= A_REVERSE)   : (off_attrs |= A_REVERSE);
			(cell.attrs.conceal)   ? (on_attrs |= A_INVIS)     : (off_attrs |= A_INVIS);
			(cell.attrs.strike)    ? (on_attrs |= A_DIM)       : (off_attrs |= A_DIM); //ncurses lacks a strikethrough bit, A_DIM is the standard fallback
			//if (cell.attrs.font); //TODO? alternative fonts
			//if (cell.attrs.dwl); if (cell.attrs.dhl); //TODO? double-width/double-height lines
			//if (cell.attrs.small); //TODO? small font
			//if (cell.attrs.baseline); //TODO? baseline shift
			//color mapping
			short fg = -1; //-1 represents the terminal's default color
			if (cell.fg.type == VTERM_COLOR_INDEXED) {
				fg = cell.fg.indexed.idx;
			} else if (cell.fg.type == VTERM_COLOR_RGB) {
				//approximate RGB to standard xterm 256-color palette index
				fg = 16 + ((cell.fg.rgb.red * 5 + 127) / 255) * 36 +
				          ((cell.fg.rgb.green * 5 + 127) / 255) * 6 +
				          ((cell.fg.rgb.blue * 5 + 127) / 255);
			}
			short bg = -1; //-1 represents the terminal's default color
			if (cell.bg.type == VTERM_COLOR_INDEXED) {
				bg = cell.bg.indexed.idx;
			} else if (cell.bg.type == VTERM_COLOR_RGB) {
				//approximate RGB to standard xterm 256-color palette index
				bg = 16 + ((cell.bg.rgb.red * 5 + 127) / 255) * 36 +
				          ((cell.bg.rgb.green * 5 + 127) / 255) * 6 +
				          ((cell.bg.rgb.blue * 5 + 127) / 255);
			}
			//dynamically allocate and cache ncurses color pairs
			//map -1 (default) to index 255 for the array bounds
			int fg_idx = (fg == -1) ? 255 : (fg % 256);
			int bg_idx = (bg == -1) ? 255 : (bg % 256);
			short pair = pair_map[fg_idx][bg_idx];
			//allocate new pair if we haven't seen this combo and haven't hit the terminal's pair limit
			if (pair == 0 && next_pair < COLOR_PAIRS) {
				init_pair(next_pair, fg, bg);
				pair_map[fg_idx][bg_idx] = next_pair;
				pair = next_pair;
				next_pair++;
			}
			if (pair > 0) {
				on_attrs |= COLOR_PAIR(pair);
			} else {
				off_attrs |= A_COLOR;
			}
			wattron(widget->window, on_attrs);
			wattroff(widget->window, off_attrs);
			char utf8[VTERM_MAX_CHARS_PER_CELL * MB_CUR_MAX + 1];
			int len = 0;
			if (cell.chars[0] == 0 || cell.chars[0] == (uint32_t)-1) {
				utf8[len++] = ' ';
			} else {
				int i = 0;
				while (i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0) {
					mbstate_t state;
					memset(&state, 0, sizeof(state));
					int l = wcrtomb(&utf8[len], cell.chars[i], &state);
					if (l > 0) len += l;
					i++;
				}
			}
			utf8[len] = '\0';
			if (len > 0) mvwaddstr(widget->window, pos.row, pos.col, utf8);
#endif
		}
	}
#ifndef USE_NOTCURSES
	//clear ncurses attributes
	attr_t clear_attrs = A_REVERSE | A_BOLD | A_UNDERLINE | A_BLINK | A_INVIS | A_DIM | A_COLOR;
#ifdef A_ITALIC //italic not supported in all versions of ncurses
	clear_attrs |= A_ITALIC;
#endif
	wattroff(widget->window, clear_attrs);
#endif
	draw_unlock(); //lift the draw lock
}

static inline void draw_external_command(struct widget* widget, int h, int w, const char* cmd) {
	int master;
	//setup the pseudo-terminal
	struct winsize ws;
	ws.ws_row = h;
	ws.ws_col = w;
	ws.ws_xpixel = 0;
	ws.ws_ypixel = 0;
	pid_t pid = forkpty(&master, NULL, NULL, &ws);
	if (pid == -1) {
		return;
	}
	if (pid == 0) {
		reset_child_sigmask(); //don't leak our blocked SIGWINCH into the spawned command
		execl("/bin/sh", "sh", "-c", cmd, NULL);
		exit(1);
	}
	//setup virtual terminal
	VTerm *vt = vterm_new(h + 1, w); //extra line for trailing newlines
	vterm_set_utf8(vt, 1);
	VTermState *state = vterm_obtain_state(vt);
	vterm_state_set_bold_highbright(state, 0); //disable bold attribute modifying color
	VTermScreen *vts = vterm_obtain_screen(vt);
	vterm_screen_reset(vts, 1);
	struct external_command_resources res = { master, pid, vt };
	pthread_cleanup_push(ec_cleanup, &res); //register ec_cleanup so resources are released on normal exit (pop) or if the thread is cancelled
	//move output child -> buffer -> virtual terminal
	char buf[1024];
	int n;
	struct pollfd pfd;
	pfd.fd = master;
	pfd.events = POLLIN;
	while (1) {
		int poll_return = poll(&pfd, 1, -1);
		if (poll_return < 0) {
			if (errno == EINTR) continue; //interrupted by signal, retry
			break;
		}
		n = read(master, buf, sizeof(buf));
		if (n < 0 && errno == EINTR) continue; //interrupted by signal, retry
		if (n <= 0) break; //EOF or child closed the PTY
		vterm_input_write(vt, buf, n);
	}
	render_vterm_screen(widget, vts, h, w);
	pthread_cleanup_pop(1);	//runs ec_cleanup: kills/reaps the child, closes master, frees vt
	stage_refresh(widget);
}

//regular version, for short-lived commands
void* external_command(void* input){
	struct widget* widget = input;
	int h, w;
	get_size(widget, &h, &w);
	int t = widget->time;
	if (t <= 0) {
		draw_external_command(widget, h, w, widget->data);
	} else {
		while(1) {
			draw_external_command(widget, h, w, widget->data);
			sleep(t);
		}
	}
	return NULL;
}

//like external_command, but the command is a template whose size placeholders are substituted with this widget's size before each run
void* dynamic_external_command(void* input){
	struct widget* widget = input;
	int h, w;
	get_size(widget, &h, &w);
	int ph, pw;
	get_pixel_size(widget, &ph, &pw);
	char* cmd = substitute_placeholders(widget->data, w, h, pw, ph);
	if(!cmd) return NULL;
	int t = widget->time;
	if (t <= 0) {
		draw_external_command(widget, h, w, cmd);
		free(cmd);
	} else {
		pthread_cleanup_push(free, cmd); //free cmd on thread cancel
		while(1) {
			draw_external_command(widget, h, w, cmd);
			sleep(t);
		}
		pthread_cleanup_pop(1);	//unreachable, balances pthread_cleanup_push macro
	}
	return NULL;
}

//shared body for the "live" variants: launch cmd once and stream its live output into the display as it arrives. never returns, the thread is torn down via pthread_cancel
static void run_live_external_command(struct widget* widget, int h, int w, const char* cmd){
	const int drain_cap = 64*1024; //max bytes drained per frame so a flooding command can't starve rendering
	int master;
	//setup the pseudo-terminal
	struct winsize ws;
	ws.ws_row = h;
	ws.ws_col = w;
	ws.ws_xpixel = 0;
	ws.ws_ypixel = 0;
	pid_t pid = forkpty(&master, NULL, NULL, &ws);
	if (pid == -1) return;
	if (pid == 0) {
		reset_child_sigmask(); //don't leak our blocked SIGWINCH into the spawned command
		execl("/bin/sh", "sh", "-c", cmd, NULL);
		exit(1);
	}
	//drain the master non-blocking, so a single burst can be read fully before rendering
	int fl = fcntl(master, F_GETFL, 0);
	if (fl != -1) fcntl(master, F_SETFL, fl | O_NONBLOCK);
	//setup virtual terminal
	VTerm *vt = vterm_new(h + 1, w); //extra line for trailing newlines
	vterm_set_utf8(vt, 1);
	VTermState *state = vterm_obtain_state(vt);
	vterm_state_set_bold_highbright(state, 0); //disable bold attribute modifying color
	VTermScreen *vts = vterm_obtain_screen(vt);
	vterm_screen_reset(vts, 1);
	struct external_command_resources res = { master, pid, vt };
	pthread_cleanup_push(ec_cleanup, &res); //release resources on cancel (resize/quit) or normal exit (pop)
	char buf[1024];
	struct pollfd pfd;
	pfd.fd = master;
	pfd.events = POLLIN;
	int child_alive = 1;
	while (child_alive) {
		int poll_return = poll(&pfd, 1, -1); //block until the child produces output
		if (poll_return < 0) {
			if (errno == EINTR) continue; //interrupted by signal, retry
			break;
		}
		//drain everything currently buffered, then render a single frame
		int dirty = 0;
		int drained = 0;
		while (drained < drain_cap) {
			int n = read(master, buf, sizeof(buf));
			if (n > 0) {
				vterm_input_write(vt, buf, n);
				dirty = 1;
				drained += n;
				continue;
			}
			if (n < 0 && errno == EINTR) continue; //interrupted by signal, retry
			if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break; //nothing more for now
			child_alive = 0; //EOF or read error: child exited / closed the PTY
			break;
		}
		if (dirty) {
			render_vterm_screen(widget, vts, h, w);
			stage_refresh(widget);
		}
	}
	while (1) pause(); //child has exited: keep the final frame on screen and the thread idle until cancelled
	pthread_cleanup_pop(1);	//unreachable, balances pthread_cleanup_push macro
}

//like external_command, but the command is launched once and its live output is streamed into the display as it arrives. widget->time is ignored
void* live_external_command(void* input){
	struct widget* widget = input;
	int h, w;
	get_size(widget, &h, &w);
	run_live_external_command(widget, h, w, widget->data);
	return NULL;
}

//like live_external_command, but the command is a template whose size placeholders are substituted with this widget's size before the single launch
void* dynamic_live_external_command(void* input){
	struct widget* widget = input;
	int h, w;
	get_size(widget, &h, &w);
	int ph, pw;
	get_pixel_size(widget, &ph, &pw);
	char* cmd = substitute_placeholders(widget->data, w, h, pw, ph);
	if(!cmd) return NULL;
	pthread_cleanup_push(free, cmd); //free cmd on thread cancel (run_live_external_command never returns)
	run_live_external_command(widget, h, w, cmd);
	pthread_cleanup_pop(1);	//unreachable, balances pthread_cleanup_push macro
	return NULL;
}
