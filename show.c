#define CURRENT_VERSION "0.0"

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <ncurses.h>
#include <pthread.h>
#include <locale.h>

#include<unistd.h>

static void* update_function(){
	while(1){
		time_t t = time(NULL);
		struct tm* tm = localtime(&t);
		char time_str[40];

		setlocale(LC_TIME, "ja_JP.utf8");	//change time locale to japanese
		strftime(time_str, 40, "%Y-%m-%d %a %H:%M:%S", tm);
		mvprintw(0, 0, time_str);
		setlocale(LC_TIME, "");			//change time locate back

		sleep(1);
	}
	return NULL;
}

static void* input_function(){
	int c;
	while(1){
		c=getch();
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
			printf("Unknown argument %s\n", argv[i]);
			return 1;
		}
	}

	//initialize program
	setlocale(LC_ALL, "en_US.UTF-8");	//so that unicode characters work
	initscr();	//initialize ncurses
	noecho();	//don't echo user input
	curs_set(0);	//set cursor invisible
	nodelay(stdscr,TRUE);	//make getch() non-blocking

	//create pthreads
	pthread_t input_thread, update_thread;
	pthread_create(&input_thread, NULL, input_function, NULL);
	pthread_create(&update_thread, NULL, update_function, NULL);

	pthread_join(input_thread, NULL);	//wait for input thread to return
	pthread_cancel(update_thread);	//cancel update thread
	pthread_join(update_thread, NULL);	//wait for update thread to cancel
	endwin();	//close ncurses
	return 0;
}
