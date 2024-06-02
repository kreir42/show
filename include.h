#define CURRENT_VERSION "0.1"

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <pthread.h>
#include <locale.h>
#include <stdlib.h>

#ifdef USE_NOTCURSES
#include <notcurses/notcurses.h>
#else
#include <ncurses.h>
#endif

#include <unistd.h>

struct rule{
	void* (*function)(void* input);	//function that will run the rule
	int y, x;	//top-left corner position
	int h, w;
	int time;
#ifdef USE_NOTCURSES
	struct ncplane* plane;
#else
	WINDOW* window;
#endif
	int_least8_t flags;
		#define CENTER_Y	1
		#define CENTER_X	2
	void* data;
};

void draw_box(int y, int x, int h, int w){
#ifdef USE_NOTCURSES
#else
//	//corners
	mvaddch(    y,     x, ACS_ULCORNER);
	mvaddch(    y, w-1+x, ACS_URCORNER);
	mvaddch(h-1+y,     x, ACS_LLCORNER);
	mvaddch(h-1+y, w-1+x, ACS_LRCORNER);
//	//sides
	mvhline(    y,   1+x, ACS_HLINE, w-2);
	mvhline(h-1+y,   1+x, ACS_HLINE, w-2);
	mvvline(  1+y,     x, ACS_VLINE, h-2);
	mvvline(  1+y, w-1+x, ACS_VLINE, h-2);
#endif
}

//shows the result of a shell command
void* external_command(void* input){
	struct rule* rule = input;
	int h = rule->h;
	int w = rule->w+1;	//+1 for the NULL terminator

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
#ifdef USE_NOTCURSES
#else
		wnoutrefresh(rule->window);
#endif
		sleep(rule->time);
	}
	return NULL;
}

//shows the date and time in a user-configured string
void* timedate(void* input){
	struct rule* rule = input;
	int w = rule->w+1;	//+1 for the NULL terminator

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
