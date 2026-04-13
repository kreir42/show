#include <sys/stat.h>
#include <time.h>

static inline void draw_image(struct rule* rule, const char* path) {
	struct ncvisual* visual = ncvisual_from_file(path);
	if (visual) {
		struct ncvisual_options vopts = {
			.n = rule->window,
			.scaling = NCSCALE_SCALE,
			.y = NCALIGN_CENTER,
			.x = NCALIGN_CENTER,
			.blitter = NCBLIT_DEFAULT,
			.flags = NCVISUAL_OPTION_HORALIGNED | NCVISUAL_OPTION_VERALIGNED,
		};
		ncvisual_blit(nc, visual, &vopts);
		ncvisual_destroy(visual);
		stage_refresh(rule);
	}
}

void* image(void* input){
	struct rule* rule = input;
	char* path = rule->data;
	int t = rule->time;

	if (t <= 0) {
		draw_image(rule, path);
	} else {
		time_t last_mtime = 0;
		while(1){
			struct stat st;
			if(stat(path, &st) == 0 && st.st_mtime > last_mtime){
				last_mtime = st.st_mtime;
				draw_image(rule, path);
			}
			sleep(t);
		}
	}
	return NULL;
}
