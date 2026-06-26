#ifdef USE_NOTCURSES
#include <sys/stat.h>
#include <time.h>

static inline void draw_image(struct widget* widget, const char* path, ncblitter_e blitter) {
	struct ncvisual* visual = ncvisual_from_file(path);
	if (visual) {
		struct ncvisual_options vopts = {
			.n = widget->window,
			.scaling = NCSCALE_SCALE,
			.y = NCALIGN_CENTER,
			.x = NCALIGN_CENTER,
			.blitter = blitter,
			.flags = NCVISUAL_OPTION_HORALIGNED | NCVISUAL_OPTION_VERALIGNED,
		};
		draw_lock();
		ncvisual_blit(nc, visual, &vopts);
		draw_unlock();
		ncvisual_destroy(visual);
		stage_refresh(widget);
	}
}

//shared body: draw widget->data once (time<=0) or re-render whenever its mtime increases, using the given blitter
static void run_image(struct widget* widget, ncblitter_e blitter){
	char* path = widget->data;
	int t = widget->time;

	if (t <= 0) {
		draw_image(widget, path, blitter);
	} else {
		time_t last_mtime = 0;
		while(1){
			struct stat st;
			if(stat(path, &st) == 0 && st.st_mtime > last_mtime){
				last_mtime = st.st_mtime;
				draw_image(widget, path, blitter);
			}
			sleep(t);
		}
	}
}

//displays an image file, preferring real pixel graphics (sixel/kitty) where the terminal supports it, falling back to the best cell blitter otherwise
void* image(void* input){
	struct widget* widget = input;
	run_image(widget, pixel_support>0 ? NCBLIT_PIXEL : NCBLIT_DEFAULT);
	return NULL;
}

//like image, but always uses the default cell blitter (colored blocks/quadrants/sextants), never pixel graphics
void* image_cells(void* input){
	struct widget* widget = input;
	run_image(widget, NCBLIT_DEFAULT);
	return NULL;
}
#endif
