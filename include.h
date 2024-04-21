#define CURRENT_VERSION "0.0"

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <ncurses.h>
#include <pthread.h>
#include <locale.h>
#include <stdlib.h>

#include <unistd.h>

struct rule{
	void* (*function)(void*);	//function that will run the rule. input is pointer to the rule struct
	int y, x, h, w;	//top-left corner position, height, width
	int time;
	void* data;
};

//function decides the placement, size for each rule
void get_size(struct rule* rule, int* y, int* x, int* w, int* h){
	*y = rule->y;
	*x = rule->x;
	if(rule->w<=0) *w = 1001;	//TBD:temporary
	else *w = rule->w;
	if(rule->h<=0) *h = 100;	//TBD:temporary
	else *h = rule->h;
}


void* external_command(void* input){
	struct rule* rule = input;
	int y, x, w, h;
	get_size(rule, &y, &x, &w, &h);

	FILE* fp;
	char* str = malloc(w*sizeof(char));
	while(1){
		fp = popen(rule->data, "r");
		for(unsigned char i=0; i<h; i++){
			if(fgets(str, w, fp)==NULL) break;	//exit early if command output ends
			mvaddstr(y+i, x, str);
		}
		pclose(fp);
		refresh();
		sleep(rule->time);
	}
	return NULL;
}

void* timedate(void* input){
	struct rule* rule = input;
	int y, x, w, h;
	get_size(rule, &y, &x, &w, &h);

        time_t t;
        struct tm* tm;
	char* str = malloc(w*sizeof(char));
	while(1){
		t = time(NULL);
		tm = localtime(&t);
		strftime(str, w, rule->data, tm);
		mvprintw(y, x, str);
		refresh();
		sleep(rule->time);
	}
}
