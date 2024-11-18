static const unsigned char rules_n = sizeof(rules) / sizeof(struct rule);

static pthread_t update_thread;
static pthread_t* rule_threads;

#ifdef USE_NOTCURSES
struct notcurses* nc;	//notcurses program
#ifdef BACKGROUND
static struct ncvisual* background_visual;
#endif
#endif

static void process_rules(){
	unsigned int max_h, max_w;
	int y, x, h, w;
#ifdef USE_NOTCURSES
	int move_y, move_x;
	notcurses_stddim_yx(nc, &max_h, &max_w);
#else
	getmaxyx(stdscr, max_h, max_w);
#endif
	for(unsigned char i=0; i<rules_n; i++){
		if(rules[i].flags&RELATIVE_Y_POS) y = rules[i].y*max_h;
		else y = rules[i].y;
		if(rules[i].flags&RELATIVE_X_POS) x = rules[i].x*max_w;
		else x = rules[i].x;
		if(rules[i].flags&RELATIVE_Y_SIZE) h = rules[i].h*max_h;
		else h = rules[i].h;
		if(rules[i].flags&RELATIVE_X_SIZE) w = rules[i].w*max_w;
		else w = rules[i].w;
#ifdef USE_NOTCURSES
		struct ncplane_options plane_options = {};
		//set notcurses plane options
		if(rules[i].flags&CENTER_Y){
			move_y = y;
			plane_options.y = NCALIGN_CENTER;
			plane_options.flags |= NCPLANE_OPTION_VERALIGNED;
		}else{
			move_y = 0;
			plane_options.y = y;
		}
		if(rules[i].flags&CENTER_X){
			move_x = x;
			plane_options.x = NCALIGN_CENTER;
			plane_options.flags |= NCPLANE_OPTION_HORALIGNED;
		}else{
			move_x = 0;
			plane_options.x = x;
		}
		plane_options.rows = h;
		plane_options.cols = w;
		rules[i].plane = ncplane_create(notcurses_stdplane(nc), &plane_options);	//create plane
		ncplane_move_rel(rules[i].plane, move_y, move_x);
		if(!(rules[i].flags&OPAQUE_BACKGROUND)){
			nccell base_cell = NCCELL_TRIVIAL_INITIALIZER;
			nccell_set_bg_alpha(&base_cell, NCALPHA_TRANSPARENT);
			ncplane_set_base_cell(rules[i].plane, &base_cell);
		}

		if(rules[i].flags&DRAW_BOX) draw_box(rules[i].plane);
#else
		if(rules[i].flags&CENTER_Y) y += (max_h-h)/2;
		if(rules[i].flags&CENTER_X) x += (max_w-w)/2;
		rules[i].window = newwin(h, w, y, x);
		if(rules[i].flags&DRAW_BOX) draw_box(rules[i].window);
#endif
	}
}

static void* update_function(){
#ifdef USE_NOTCURSES
	struct ncplane* stdplane = notcurses_stdplane(nc);
#endif
	while(1){
#ifdef USE_NOTCURSES
		ncpile_render(stdplane);
		ncpile_rasterize(stdplane);
#else
		doupdate();
#endif
		usleep(REFRESH_MICROSECONDS);	//TBD:change to nanosleep?
	}
	return NULL;
}

static void start_display(){
#ifdef USE_NOTCURSES
#ifdef BACKGROUND
	background_visual = ncvisual_from_file(BACKGROUND);
	struct ncvisual_options ncvisual_options = {
		.n = notcurses_stdplane(nc),
		.scaling = NCSCALE_SCALE,
		.y = NCALIGN_CENTER, .x = NCALIGN_CENTER,
		.blitter = BACKGROUND_BLIT,
		.flags = NCVISUAL_OPTION_HORALIGNED|NCVISUAL_OPTION_VERALIGNED | NCVISUAL_OPTION_CHILDPLANE,
	};
	struct ncplane* background_plane = ncvisual_blit(nc, background_visual, &ncvisual_options);
#endif
#endif
	process_rules();
	//create a pthread per rule
	for(unsigned char i=0; i<rules_n; i++){
		pthread_create(&rule_threads[i], NULL, rules[i].function, &rules[i]);
	}
	//create a pthread to update the screen
	pthread_create(&update_thread, NULL, update_function, NULL);
}

static void end_display(){
	//cancel all threads other than input
	pthread_cancel(update_thread);
	for(unsigned char i=0; i<rules_n; i++){
		pthread_cancel(rule_threads[i]);
	}
	//wait for them to cancel
	pthread_join(update_thread, NULL);
	for(unsigned char i=0; i<rules_n; i++){
		pthread_join(rule_threads[i], NULL);
#ifndef USE_NOTCURSES
		endwin();
#endif
	}
#ifdef USE_NOTCURSES
#ifdef BACKGROUND
	ncvisual_destroy(background_visual);
#endif
	notcurses_drop_planes(nc);
	ncplane_erase(notcurses_stdplane(nc));
#else
	refresh();
	clear();
#endif
}

static void* input_function(){
#ifdef USE_NOTCURSES
	uint32_t c;
#else
	int c;
	//create a separate window so getch doesnt bock the update threads
	WINDOW* window = newwin(1,1,0,0);
#endif
	while(1){
#ifdef USE_NOTCURSES
		c=notcurses_get(nc, NULL, NULL);
#else
		c=wgetch(window);
#endif
		switch(c){
			case 'q':
			case 'Q':
				return NULL;
#ifdef USE_NOTCURSES
			case NCKEY_RESIZE:
			case NCKEY_ENTER:
#else
			case 10:
#endif
			case ' ':
				end_display();
				start_display();
		}
	}
}

void handle_winch(int sig){
	end_display();
	start_display();
}

int main(int argc, char** argv){
	//CLI arguments
	for(short i=1; i<argc; i++){
		if(!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")){
			printf("Flags:\n");
			printf("     --help or -h\n");
			printf("       Print this message and exit.\n");
			printf("     --version or -v\n");
			printf("       Print the code version and exit.\n");
			printf("Keys:\n");
			printf("     'q': quit program\n");
			printf("     ' ' or ENTER: refresh display\n");
			return 0;
		}else if(!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")){
			printf(CURRENT_VERSION"\n");
			return 0;
		}else{
			printf("Unknown argument %s, use -h or --help for valid arguments\n", argv[i]);
			return 1;
		}
	}

	//initialize program
	setlocale(LC_ALL, PROGRAM_LOCALE);	//necessary for ncurses/notcurses
#ifdef USE_NOTCURSES
	struct notcurses_options opts = {
		.flags = NCOPTION_SUPPRESS_BANNERS,
	};
	nc = notcurses_init(&opts, NULL);	//initialize notcurses
#else
	initscr();
	noecho();
	curs_set(0);
	//handler SIGWINCH signal
	struct sigaction sa;
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = handle_winch;
	sigaction(SIGWINCH, &sa, NULL);
#endif

	pthread_t input_thread;
	rule_threads = malloc(sizeof(pthread_t)*rules_n);
	//create pthread for input
	pthread_create(&input_thread, NULL, input_function, NULL);

	start_display();

#ifndef USE_NOTCURSES
	end_display();
	start_display();
#endif

	pthread_join(input_thread, NULL);	//wait for input thread to return

	end_display();

	free(rule_threads);

#ifdef USE_NOTCURSES
	return notcurses_stop(nc);	//close notcurses, return 0 if success
#else
	for(unsigned char i=0; i<rules_n; i++){
		endwin();
	}
	endwin();
	endwin();
	return 0;
#endif
}
