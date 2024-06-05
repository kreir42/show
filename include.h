#define CURRENT_VERSION "0.1"

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <pthread.h>
#include <locale.h>
#include <stdlib.h>

#ifdef USE_NOTCURSES
#include <notcurses/notcurses.h>
extern struct notcurses* nc;
#define BOX_HLINE "─"
#define BOX_VLINE "│"
#define BOX_URCORNER "┐"
#define BOX_ULCORNER "┌"
#define BOX_LRCORNER "┘"
#define BOX_LLCORNER "└"
#else
#include <ncurses.h>
#include <signal.h>
#endif

#include <unistd.h>

struct rule{
	void* (*function)(void* input);	//function that will run the rule
	float y, x;	//top-left corner position
	float h, w;
	int time;
#ifdef USE_NOTCURSES
	struct ncplane* plane;
#else
	WINDOW* window;
#endif
	int_least8_t flags;
		#define CENTER_Y	 1
		#define CENTER_X	 2
		#define RELATIVE_Y_POS	 4
		#define RELATIVE_X_POS	 8
		#define RELATIVE_Y_SIZE	16
		#define RELATIVE_X_SIZE	32
		#define DRAW_BOX	64
		#define CENTER		CENTER_Y|CENTER_X
		#define RELATIVE_POS	RELATIVE_Y_POS|RELATIVE_X_POS
		#define RELATIVE_SIZE	RELATIVE_Y_SIZE|RELATIVE_X_SIZE
	void* data;
};

#ifdef USE_NOTCURSES
void draw_box(struct ncplane* plane){
	int h, w;
	ncplane_dim_yx(plane, &h, &w);
	h+=2; w+=2;
	struct ncplane_options plane_options = {
		.y = -1, .x = -1,
		.rows = h, .cols = w,
	};
	struct ncplane* box_plane = ncplane_create(plane, &plane_options);
	nccell base_cell = NCCELL_TRIVIAL_INITIALIZER;
	nccell_set_bg_alpha(&base_cell, NCALPHA_TRANSPARENT);
	ncplane_set_base_cell(box_plane, &base_cell);

	nccell ul = NCCELL_TRIVIAL_INITIALIZER, ur = NCCELL_TRIVIAL_INITIALIZER;
	nccell ll = NCCELL_TRIVIAL_INITIALIZER, lr = NCCELL_TRIVIAL_INITIALIZER;
	nccell hl = NCCELL_TRIVIAL_INITIALIZER, vl = NCCELL_TRIVIAL_INITIALIZER;

	nccell_load(box_plane, &ul, BOX_ULCORNER); nccell_load(box_plane, &ur, BOX_URCORNER);
	nccell_load(box_plane, &ll, BOX_LLCORNER); nccell_load(box_plane, &lr, BOX_LRCORNER);
	nccell_load(box_plane, &hl, BOX_HLINE);    nccell_load(box_plane, &vl, BOX_VLINE);

	ncplane_perimeter(box_plane, &ul, &ur, &ll, &lr, &hl, &vl, 0);

	nccell_release(box_plane, &ul); nccell_release(box_plane, &ur);
	nccell_release(box_plane, &ll); nccell_release(box_plane, &lr);
	nccell_release(box_plane, &hl); nccell_release(box_plane, &vl);
}
#else
void draw_box(WINDOW* window){
	int y, x, h, w;
	getbegyx(window, y, x);
	getmaxyx(window, h, w);
	y--; x--;
	h+=2; w+=2;
	//corners
	mvaddch(    y,     x, ACS_ULCORNER);
	mvaddch(    y, w-1+x, ACS_URCORNER);
	mvaddch(h-1+y,     x, ACS_LLCORNER);
	mvaddch(h-1+y, w-1+x, ACS_LRCORNER);
	//sides
	mvhline(    y,   1+x, ACS_HLINE, w-2);
	mvhline(h-1+y,   1+x, ACS_HLINE, w-2);
	mvvline(  1+y,     x, ACS_VLINE, h-2);
	mvvline(  1+y, w-1+x, ACS_VLINE, h-2);
}
#endif

static void get_size(struct rule* rule, int* h, int* w){
	unsigned int max_h, max_w;
#ifdef USE_NOTCURSES
	notcurses_stddim_yx(nc, &max_h, &max_w);
#else
	getmaxyx(stdscr, max_h, max_w);
#endif
	if(rule->flags&RELATIVE_Y_SIZE) *h = rule->h*max_h;
	else *h = rule->h;
	if(rule->flags&RELATIVE_X_SIZE) *w = rule->w*max_w;
	else *w = rule->w;
}

//shows the result of a shell command
void* external_command(void* input){
	struct rule* rule = input;
	int h, w;
	get_size(rule, &h, &w);
	w++;	//+1 for the NULL terminator

	char* str = malloc(w*sizeof(char));
	FILE* fp;
	unsigned short last;
	while(1){
		fp = popen(rule->data, "r");
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
#ifdef USE_NOTCURSES
			ncplane_putstr_yx(rule->plane, i, 0, str);
#else
			mvwaddstr(rule->window, i, 0, str);
#endif
		}
		pclose(fp);
#ifndef USE_NOTCURSES
		wnoutrefresh(rule->window);
#endif
		sleep(rule->time);
	}
	return NULL;
}

//shows the date and time in a user-configured string
void* timedate(void* input){
	struct rule* rule = input;
	int h, w;
	get_size(rule, &h, &w);
	w++;	//+1 for the NULL terminator

	char* str = malloc(w*sizeof(char));
	time_t t;
	struct tm* tm;
	while(1){
		t = time(NULL);
		tm = localtime(&t);
		strftime(str, w, rule->data, tm);
#ifdef USE_NOTCURSES
		ncplane_putstr_yx(rule->plane, 0, 0, str);
#else
		mvwaddstr(rule->window, 0, 0, str);
		wnoutrefresh(rule->window);
#endif
		sleep(rule->time);
	}
	return NULL;
}
