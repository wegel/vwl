/* Preserve focus wrapping and tab order while sharing the direction loops. */
#include "compositor.h"

int
main(void)
{
	char runtime[] = "/tmp/vwl-layout-XXXXXX";
	struct TestClient app;
	Client *c, *sibling;
	VirtualOutput *vout, *other;
	Workspace *ws, *hidden;
	Arg next = {.i = 1}, prev = {.i = -1};

	test_setup(runtime, "2", NULL);
	app = test_client_start(fullscreen_test_client, 20);
	client_command(app.control, 'r');
	c = wl_container_of(clients.next, c, link);
	client_command(app.control, 'u');
	setfloating(c, 0);
	ws = c->ws;
	vout = ws->vout;
	setlayout(&(Arg){.v = &layouts[0]});
	focusclient(c, 1);
	focusstack(&next);
	assert(focustop(selmon) == c);
	focusstack(&prev);
	assert(focustop(selmon) == c);

	client_command(app.control, 'n');
	sibling = wl_container_of(clients.prev, sibling, link);
	assert(sibling != c);
	setworkspace(sibling, ws);
	setfloating(sibling, 0);
	focusclient(c, 1);
	focusstack(&next);
	assert(focustop(selmon) == sibling);
	focusstack(&next);
	assert(focustop(selmon) == c);
	focusstack(&prev);
	assert(focustop(selmon) == sibling);
	focusstack(&prev);
	assert(focustop(selmon) == c);

	setlayout(&(Arg){.v = &layouts[1]});
	assert(clients.next == &c->link);
	tabmove(&prev);
	assert(clients.next == &c->link);
	tabmove(&next);
	assert(clients.next == &sibling->link && clients.prev == &c->link);
	tabmove(&next);
	assert(clients.prev == &c->link);
	tabmove(&prev);
	assert(clients.next == &c->link && clients.prev == &sibling->link);
	assert(focustop(selmon) == c);

	/* Floating and hidden windows must not become tabs or tab-move targets. */
	setfloating(sibling, 1);
	focusclient(c, 1);
	focusstack(&next);
	focusstack(&prev);
	assert(focustop(selmon) == c);
	tabmove(&next);
	assert(clients.next == &c->link);
	setfloating(sibling, 0);
	hidden = wsfindfree();
	assert(hidden);
	wsattach(vout, hidden);
	setworkspace(sibling, hidden);
	focusclient(c, 1);
	focusstack(&next);
	focusstack(&prev);
	tabmove(&next);
	assert(focustop(selmon) == c && clients.next == &c->link);

	/* Tab focus stays inside its virtual output; tiled focus can cross it. */
	other = createvout(c->mon, "test-other");
	wsattach(other, hidden);
	wsactivate(other, hidden, 0);
	assert(ipc_focus_virtual_output(vout) == 0);
	focusstack(&next);
	focusstack(&prev);
	tabmove(&next);
	assert(focustop(selmon) == c && clients.next == &c->link);
	setlayout(&(Arg){.v = &layouts[0]});
	focusstack(&next);
	assert(focustop(selmon) == sibling);
	focusstack(&prev);
	assert(focustop(selmon) == c);

	test_client_finish(app);
	test_cleanup(runtime);
	puts("layout: focus wrapping, tab moves, floating/hidden windows and virtual outputs passed");
	return 0;
}
