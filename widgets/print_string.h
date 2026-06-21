//print a string
void* print_string(void* input){
	struct widget* widget = input;
	draw_string(widget, 0, 0, widget->data);
	stage_refresh(widget);
	return NULL;
}

//print an ASCII string in large block letters scaled to fill the widget, using half-block glyphs
void* print_large_string(void* input){
	struct widget* widget = input;
	draw_big_string(widget, widget->data);
	return NULL;
}
