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
	timedate,          0,  0,  -1,  -1,       1, "%Y-%m-%d %a %H:%M:%S",
	external_command,  1,  0,  -1,  -1,     600, "cal",
};

//////////////////////
//////// CODE ////////
//////////////////////

static const unsigned char rules_n = sizeof(rules) / sizeof(struct rule);

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
			printf("Unknown argument %s\n", argv[i]);
			return 1;
		}
	}

	//initialize program
	setlocale(LC_ALL, PROGRAM_LOCALE);	//so that unicode characters work
	initscr();	//initialize ncurses
	noecho();	//don't echo user input
	curs_set(0);	//set cursor invisible

	//create pthread for input
	pthread_t input_thread;
	pthread_create(&input_thread, NULL, input_function, NULL);
	//create a pthread per rule
	pthread_t update_threads[rules_n];
	for(unsigned char i=0; i<rules_n; i++){
		pthread_create(&update_threads[i], NULL, rules[i].function, &rules[i]);
	}

	pthread_join(input_thread, NULL);	//wait for input thread to return
	for(unsigned char i=0; i<rules_n; i++){	//cancel all threads
		pthread_cancel(update_threads[i]);
	}
	for(unsigned char i=0; i<rules_n; i++){	//wait for update threads to cancel
		pthread_join(update_threads[i], NULL);
	}
	endwin();	//close ncurses
	return 0;
}
