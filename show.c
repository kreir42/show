#include "include.h"

//////////////////////
/////// CONFIG ///////
//////////////////////

#define PROGRAM_LOCALE ""
#define REFRESH_MICROSECONDS 250000

static struct rule rules[] = {
//	                  |  -1 for auto  |
//	                   \_______       |
//	                           |      |
//	function           y   x    h    w  time (s) NULL
	timedate,          0, 10,   1,  23,       1, NULL, "%Y-%m-%d %a %H:%M:%S",
	external_command,  1,  0,   8,  42,       2, NULL, "cal -m -n 2 --color=never",
};

//////////////////////
//////// CODE ////////
//////////////////////

static const unsigned char rules_n = sizeof(rules) / sizeof(struct rule);

static struct notcurses* nc;	//notcurses program

static void process_rules(){
	struct ncplane_options plane_options = {};
	unsigned int max_h, max_w;
	notcurses_stddim_yx(nc, &max_h, &max_w);
	for(unsigned char i=0; i<rules_n; i++){
		if(rules[i].h<1) rules[i].h = -(max_h - rules[i].y);	//TBD: temporary
		if(rules[i].w<1) rules[i].w = -(max_w - rules[i].x);
		//set notcurses plane options
		plane_options.y = rules[i].y;
		plane_options.x = rules[i].x;
		plane_options.rows = rules[i].h>0?rules[i].h:-rules[i].h;
		plane_options.cols = rules[i].w>0?rules[i].w:-rules[i].w;
		rules[i].plane = ncplane_create(notcurses_stdplane(nc), &plane_options);	//create plane
	}
//	draw_box(0, 0, max_h, max_w);
}

static void* update_function(){
	struct ncplane* stdplane = notcurses_stdplane(nc);
	while(1){
		ncpile_render(stdplane);
		ncpile_rasterize(stdplane);
		usleep(REFRESH_MICROSECONDS);	//TBD:change to nanosleep?
	}
	return NULL;
}

static void* input_function(){
	uint32_t c;
	//create a separate window so getch doesnt bock the update threads
	while(1){
		c=notcurses_get(nc, NULL, NULL);
		switch(c){
			case 'q':
			case 'Q':
				return NULL;
			case 10:
			case ' ':
				//TBD: update
		}
	}
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
	setlocale(LC_ALL, PROGRAM_LOCALE);	//necessary for notcurses
	struct notcurses_options opts = {
		.flags = NCOPTION_SUPPRESS_BANNERS,
	};
	nc = notcurses_core_init(&opts, NULL);	//initialize notcurses

	pthread_t input_thread, update_thread;
	pthread_t rule_threads[rules_n];
	//create pthread for input
	pthread_create(&input_thread, NULL, input_function, NULL);
	process_rules();	//initial setup
	//create a pthread per rule
	for(unsigned char i=0; i<rules_n; i++){
		pthread_create(&rule_threads[i], NULL, rules[i].function, &rules[i]);
	}
	//create a pthread to update the screen
	pthread_create(&update_thread, NULL, update_function, NULL);

	pthread_join(input_thread, NULL);	//wait for input thread to return
	//cancel all other threads
	pthread_cancel(update_thread);
	for(unsigned char i=0; i<rules_n; i++){
		pthread_cancel(rule_threads[i]);
	}
	//wait for all other threads to cancel
	pthread_join(update_thread, NULL);
	for(unsigned char i=0; i<rules_n; i++){
		pthread_join(rule_threads[i], NULL);
	}
	return notcurses_stop(nc);	//close notcurses, return 0 if success
}
