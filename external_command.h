//shows the result of a shell command
static inline void draw_text_external_command(struct rule* rule, int h, int w, char* str) {
	FILE* fp = popen(rule->data, "r");
	unsigned short last;
	for(unsigned short i=0; i<h; i++){
		if(fgets(str, w, fp)==NULL) break;	//exit early if command output ends
		//if last char is a newline, remove
		last = strlen(str)-1;
		if(str[last]=='\n'){
			str[last] = '\0';
			if(last==0){	//first and only char was a newline
				i--;
				continue;
			}
		}
		draw_string(rule, i, 0, str);
	}
	pclose(fp);
	stage_refresh(rule);
}

void* text_external_command(void* input){
	struct rule* rule = input;
	int h, w;
	get_size(rule, &h, &w);
	w++;	//+1 for the NULL terminator
	char* str = malloc(w*sizeof(char));
	int t = rule->time;
	if (t <= 0) {
		draw_text_external_command(rule, h, w, str);
	} else {
		while(1){
			draw_text_external_command(rule, h, w, str);
			sleep(t);
		}
	}
	free(str);
	return NULL;
}

#include <pty.h>
#include <vterm.h>
#include <unistd.h>
#include <sys/wait.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static inline void draw_external_command(struct rule* rule, int h, int w) {
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
		execl("/bin/sh", "sh", "-c", rule->data, NULL);
		exit(1);
	}
	//setup virtual terminal
	VTerm *vt = vterm_new(h + 1, w); //extra line for trailing newlines
	vterm_set_utf8(vt, 1);
	VTermState *state = vterm_obtain_state(vt);
	vterm_state_set_bold_highbright(state, 0); //disable bold attribute modifying color
	VTermScreen *vts = vterm_obtain_screen(vt);
	vterm_screen_reset(vts, 1);
	//move output child -> buffer -> virtual terminal
	char buf[1024];
	int n;
	struct pollfd pfd;
	pfd.fd = master;
	pfd.events = POLLIN;
	while (poll(&pfd, 1, -1) > 0) {
		n = read(master, buf, sizeof(buf));
		if (n <= 0) break;
		vterm_input_write(vt, buf, n);
	}
	int status;
	waitpid(pid, &status, 0);
	close(master);

#ifndef USE_NOTCURSES
	//if ncurses, setup cache for color pairs
	short next_pair = 1;
	short pair_map[256][256] = {0}; //cache for up to 256x256 combinations
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
			while (cell.chars[i] != 0 && i < VTERM_MAX_CHARS_PER_CELL) {
				mbstate_t state; memset(&state, 0, sizeof(state)); int l = wcrtomb(utf8 + len, cell.chars[i], &state); //wide character to multi-byte
				if (l > 0) len += l;
				i++;
			}
			if (i == 0) continue;
			utf8[len] = '\0';

			nccell nc_cell;
			nccell_init(&nc_cell);
			if (nccell_load(rule->window, &nc_cell, utf8) < 0) continue; //skip if failed
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
			ncplane_putc_yx(rule->window, pos.row, pos.col, &nc_cell);
			nccell_release(rule->window, &nc_cell);
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
			wattron(rule->window, on_attrs);
			wattroff(rule->window, off_attrs);
			char utf8[VTERM_MAX_CHARS_PER_CELL * MB_CUR_MAX + 1];
			int len = 0;
			if (cell.chars[0] == 0 || cell.chars[0] == (uint32_t)-1) {
				utf8[len++] = ' ';
			} else {
				int i = 0;
				while (cell.chars[i] != 0 && i < VTERM_MAX_CHARS_PER_CELL) {
					mbstate_t state;
					memset(&state, 0, sizeof(state));
					int l = wcrtomb(&utf8[len], cell.chars[i], &state);
					if (l > 0) len += l;
					i++;
				}
			}
			utf8[len] = '\0';
			if (len > 0) mvwaddstr(rule->window, pos.row, pos.col, utf8);
#endif
		}
	}
#ifndef USE_NOTCURSES
	//clear ncurses attributes
	attr_t clear_attrs = A_REVERSE | A_BOLD | A_UNDERLINE | A_BLINK | A_INVIS | A_DIM | A_COLOR;
#ifdef A_ITALIC //italic not supported in all versions of ncurses
	clear_attrs |= A_ITALIC;
#endif
	wattroff(rule->window, clear_attrs);
#endif
	vterm_free(vt);
	stage_refresh(rule);
}

void* external_command(void* input){
	struct rule* rule = input;
	int h, w;
	get_size(rule, &h, &w);
	int t = rule->time;
	if (t <= 0) {
		draw_external_command(rule, h, w);
	} else {
		while(1) {
			draw_external_command(rule, h, w);
			sleep(t);
		}
	}
	return NULL;
}
