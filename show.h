static const unsigned short rules_n = sizeof(rules) / sizeof(struct rule);

static pthread_t update_thread;
static pthread_t* rule_threads;

#ifdef USE_NOTCURSES
struct notcurses* nc;	//notcurses program
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
	for(unsigned short i=0; i<rules_n; i++){
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
		rules[i].window = ncplane_create(notcurses_stdplane(nc), &plane_options);	//create plane
		ncplane_move_rel(rules[i].window, move_y, move_x);
		nccell base_cell = NCCELL_TRIVIAL_INITIALIZER;
		if(rules[i].flags&OPAQUE){
			nccell_set_bg_alpha(&base_cell, NCALPHA_OPAQUE);
		}else if(rules[i].flags&BLEND_BACKGROUND){
			nccell_set_bg_alpha(&base_cell, NCALPHA_BLEND);
		}else{
			nccell_set_bg_alpha(&base_cell, NCALPHA_TRANSPARENT);
		}
		nccell_set_fg_alpha(&base_cell, NCALPHA_OPAQUE);
		if(rules[i].flags&BOLD) {
			nccell_on_styles(&base_cell, NCSTYLE_BOLD);
			ncplane_on_styles(rules[i].window, NCSTYLE_BOLD);
		}
		if(rules[i].flags&ITALIC) {
			nccell_on_styles(&base_cell, NCSTYLE_ITALIC);
			ncplane_on_styles(rules[i].window, NCSTYLE_ITALIC);
		}
		ncplane_set_base_cell(rules[i].window, &base_cell);

		if(rules[i].flags&DRAW_BOX) draw_box(rules[i].window);
#else
		if(rules[i].flags&CENTER_Y) y += (max_h-h)/2;
		if(rules[i].flags&CENTER_X) x += (max_w-w)/2;
		rules[i].window = newwin(h, w, y, x);
		if(rules[i].flags&BOLD) wattron(rules[i].window, A_BOLD);
#ifdef A_ITALIC
		if(rules[i].flags&ITALIC) wattron(rules[i].window, A_ITALIC);
#endif
		if(rules[i].flags&DRAW_BOX) draw_box(rules[i].window);
#endif
	}
}

static void* update_function(void* _){
#ifdef USE_NOTCURSES
	struct ncplane* stdplane = notcurses_stdplane(nc);
#endif
	while(1){
#ifdef USE_NOTCURSES
		ncpile_render(stdplane);
		ncpile_rasterize(stdplane);
#else
		draw_lock();
		doupdate();
		draw_unlock();
#endif
		usleep(REFRESH_MICROSECONDS);	//TBD:change to nanosleep?
	}
	return NULL;
}

static void start_display(){
	process_rules();
	//create a pthread per rule
	for(unsigned short i=0; i<rules_n; i++){
		pthread_create(&rule_threads[i], NULL, rules[i].function, &rules[i]);
	}
	//create a pthread to update the screen
	pthread_create(&update_thread, NULL, update_function, NULL);
}

static void end_display(){
	//cancel all threads other than input
	pthread_cancel(update_thread);
	for(unsigned short i=0; i<rules_n; i++){
		pthread_cancel(rule_threads[i]);
	}
	//wait for them to cancel
	pthread_join(update_thread, NULL);
	for(unsigned short i=0; i<rules_n; i++){
		pthread_join(rule_threads[i], NULL);
#ifdef USE_NOTCURSES
		if(rules[i].function == plot){
			void* userptr = ncplane_userptr(rules[i].window);
			if(userptr) ncdplot_destroy(userptr);
		}
#else
		draw_lock(); //serialize against rule threads still being cancelled that may still be drawing
		delwin(rules[i].window);
		draw_unlock();
#endif
	}
#ifdef USE_NOTCURSES
	notcurses_drop_planes(nc);
	ncplane_erase(notcurses_stdplane(nc));
#else
	endwin();
	refresh();
	clear();
#endif
}

#ifndef USE_NOTCURSES
static pthread_mutex_t rebuild_mutex = PTHREAD_MUTEX_INITIALIZER;
static void unlock_rebuild_mutex(void* m){ pthread_mutex_unlock(m); }	//void* signature for pthread_cleanup_push
#endif

//tear clean and rebuild the display. serialized so the input and resize threads can't overlap
static void rebuild_display(){
#ifndef USE_NOTCURSES
	pthread_mutex_lock(&rebuild_mutex);
	pthread_cleanup_push(unlock_rebuild_mutex, &rebuild_mutex); //release the lock even if cancelled mid-rebuild
#endif
	end_display();
	start_display();
#ifndef USE_NOTCURSES
	pthread_cleanup_pop(1);
#endif
}

static void* input_function(void* _){
#ifdef USE_NOTCURSES
	uint32_t c;
#else
	int c;
	//create a separate window so getch doesnt block the update threads
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
#ifndef USE_NOTCURSES
				delwin(window);
#endif
				return NULL;
#ifdef USE_NOTCURSES
			case NCKEY_RESIZE:
			case NCKEY_ENTER:
#else
			case 10:
#endif
			case ' ':
				rebuild_display();
		}
	}
}

#ifndef USE_NOTCURSES
//dedicated rebuild thread for SIGWINCH via sigwait
static void* resize_function(void* _){
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGWINCH);
	int sig;
	while(1){
		if(sigwait(&set, &sig) == 0) rebuild_display();
	}
	return NULL;
}
#endif

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
	if(nc == NULL){
		fprintf(stderr, "Failed to initialize notcurses\n");
		return 1;
	}
#else
	initscr();
	noecho();
	start_color();
	use_default_colors();
	curs_set(0);
	//block SIGWINCH in every thread (they inherit this mask) so it gets handled in resize_thread instead
	sigset_t winch_set;
	sigemptyset(&winch_set);
	sigaddset(&winch_set, SIGWINCH);
	pthread_sigmask(SIG_BLOCK, &winch_set, NULL);
#endif

	pthread_t input_thread;
	rule_threads = malloc(sizeof(pthread_t)*rules_n);
	if(rule_threads == NULL){
		fprintf(stderr, "Failed to allocate rule threads\n");
#ifdef USE_NOTCURSES
		notcurses_stop(nc);
#else
		endwin();
#endif
		return 1;
	}
	//create pthread for input
	pthread_create(&input_thread, NULL, input_function, NULL);
#ifndef USE_NOTCURSES
	//create pthread to handle terminal resizes (SIGWINCH signal)
	pthread_t resize_thread;
	pthread_create(&resize_thread, NULL, resize_function, NULL);
#endif

	start_display();

#ifndef USE_NOTCURSES
	rebuild_display();
#endif

	pthread_join(input_thread, NULL);	//wait for input thread to return
#ifndef USE_NOTCURSES
	pthread_cancel(resize_thread);
	pthread_join(resize_thread, NULL);
#endif
	end_display();
	free(rule_threads);

#ifdef USE_NOTCURSES
	return notcurses_stop(nc);	//close notcurses, return 0 if success
#else
	endwin();
	return 0;
#endif
}
