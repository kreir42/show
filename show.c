#include "include.h"

//////////////////////
/////// CONFIG ///////
//////////////////////

#define PROGRAM_LOCALE "en_US.UTF-8"

static struct rule rules[] = {
//	                  |  -1 for auto  |
//	                   \_______       |
//	                           |      |
//	function           y   x    h    w  time (s)
	timedate,          0, 12,   1,  23,       1, "%Y-%m-%d %a %H:%M:%S",
	external_command,  1,  0,   8,  43,      15, "cal -m -n 2 --color=never",
	external_command, 20,  0,   8,   5,       1, "echo 12345 && echo 1234567890",
};

//////////////////////
//////// CODE ////////
//////////////////////

static const unsigned char rules_n = sizeof(rules) / sizeof(struct rule);


static void* update_function(){
	//initial setup
	int max_h, max_w;
	WINDOW* windows[rules_n];
	getmaxyx(stdscr, max_h, max_w);
	for(unsigned char i=0; i<rules_n; i++){
		if(rules[i].h<1) rules[i].h = -(max_h - rules[i].y);	//TBD: temporary
		if(rules[i].w<1) rules[i].w = -(max_w - rules[i].x);
		windows[i] = newwin(rules[i].h>0?rules[i].h:-rules[i].h, rules[i].w>0?rules[i].w:-rules[i].w, rules[i].y, rules[i].x);
	}
//	draw_box(0, 0, max_h, max_w);
//	refresh();
//	doupdate();

	unsigned int time_to_sleep = 0;
	unsigned int times_left[rules_n];
	memset(times_left, 0, rules_n*sizeof(times_left[0]));
	//main loop
	while(1){
		for(unsigned char i=0; i<rules_n; i++){
			times_left[i] -= time_to_sleep;
			if(times_left[i] == 0){
				rules[i].function(&rules[i], windows[i]);
				wrefresh(windows[i]);
				times_left[i] = rules[i].time;
			}
		}
		//find min time, then sleep
		time_to_sleep = times_left[0];
		for(unsigned char i=1; i<rules_n; i++){
			if(times_left[i] < time_to_sleep) time_to_sleep = times_left[i];
		}
		sleep(time_to_sleep);
	}
	return NULL;
}

static void* input_function(){
	int c;
	//create a separate window so getch doesnt bock the update threads
	WINDOW* window = newwin(1,1,0,0);
	while(1){
		c=wgetch(window);
		switch(c){
			case 'q':
			case 'Q':
				goto while_end;
			case 10:
			case ' ':
				//TBD: update
		}
	}
	while_end:
	endwin();
	return NULL;
}

int main(int argc, char** argv){
	//CLI arguments
	for(short i=1; i<argc; i++){
		if(!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")){
			printf("Help message TBD\n");
			return 0;
		}else if(!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")){
			printf("Version "CURRENT_VERSION"\n");
		}else{
			printf("Unknown argument %s, use -h or --help for valid arguments\n", argv[i]);
			return 1;
		}
	}

	//initialize program
	setlocale(LC_ALL, PROGRAM_LOCALE);	//so that unicode characters work
	initscr();	//initialize ncurses
	noecho();	//don't echo user input
	curs_set(0);	//set cursor invisible

	pthread_t input_thread, update_thread;
	//create pthread for input
	pthread_create(&input_thread, NULL, input_function, NULL);
	//create a pthread to run the rules
	pthread_create(&update_thread, NULL, update_function, NULL);

	pthread_join(input_thread, NULL);	//wait for input thread to return
	pthread_cancel(update_thread);
	pthread_join(update_thread, NULL);
	for(unsigned char i=0; i<rules_n; i++){	//close all windows
		endwin();
	}
	endwin();	//close ncurses
	return 0;
}
