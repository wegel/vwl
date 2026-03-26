#ifndef TABHDR_H
#define TABHDR_H

#include "vwl.h"

struct TabHdrStyle {
	enum TabHdrPos position;
	const float *active_color;
	const float *inactive_color;
	const float *text_active_color;
	const float *text_inactive_color;
	const char *font;
	int padding_top;
	int padding_bottom;
	int padding_left;
	int padding_right;
	const TabTitleTransformRule *title_transforms;
};

const struct TabHdrStyle *tabhdr_style(void);
enum TabHdrPos tabhdr_position_value(void);
int tabhdr_header_height(void);
void tabhdr_disable(VirtualOutput *vout);
void tabhdr_update(Monitor *m, VirtualOutput *vout, struct wlr_box area, Client *active);

#endif
