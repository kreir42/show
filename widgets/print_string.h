//print a string
void* print_string(void* input){
	struct rule* rule = input;
	draw_string(rule, 0, 0, rule->data);
	stage_refresh(rule);
	return NULL;
}
