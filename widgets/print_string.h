//print a string
void* print_string(void* input){
	struct rule* rule = input;
	draw_string(rule, 0, 0, rule->data);
	stage_refresh(rule);
	return NULL;
}

//print an ASCII string in large block letters scaled to fill the widget, using half-block glyphs
void* print_large_string(void* input){
	struct rule* rule = input;
	draw_big_string(rule, rule->data);
	return NULL;
}
