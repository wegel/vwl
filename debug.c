/* Human-readable compositor state dump. */
#include <stdio.h>
#include <wlr/util/log.h>
#include "vwl.h"

void
debugstate(const Arg *arg)
{
	/* dump compositor state to /tmp/vwl-debug.txt for debugging */
	FILE *f;
	Monitor *m;
	VirtualOutput *vout;
	Workspace *ws;
	Client *c;
	int client_count = 0;
	time_t now;
	char timebuf[64];

	f = fopen("/tmp/vwl-debug.txt", "w");
	if (!f) {
		wlr_log(WLR_ERROR, "debugstate: failed to open /tmp/vwl-debug.txt");
		return;
	}

	now = time(NULL);
	strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
	fprintf(f, "=== vwl debug state dump ===\n");
	fprintf(f, "timestamp: %s\n", timebuf);
	fprintf(f, "selmon: %s\n", selmon ? selmon->wlr_output->name : "(null)");
	fprintf(f, "selvout: %s\n", selvout ? selvout->name : "(null)");
	fprintf(f, "selws: %s (id=%u)\n", selws ? selws->name : "(null)", selws ? selws->id : 0);
	fprintf(f, "hover_mon: %s\n", hover.mon && hover.mon->wlr_output ? hover.mon->wlr_output->name : "(null)");
	fprintf(f, "hover_vout: %s\n", hover.vout ? hover.vout->name : "(null)");
	fprintf(f, "hover_ws: %s (id=%u)\n", hover.ws ? hover.ws->name : "(null)", hover.ws ? hover.ws->id : 0);
	fprintf(f, "hover_client: appid=\"%s\" title=\"%s\"\n",
			hover.client ? client_get_appid(hover.client) : "(null)",
			hover.client ? client_get_title(hover.client) : "(null)");
	fprintf(f, "locked: %d\n", locked);
	fprintf(f, "cursor_mode: %u\n", cursor_mode);

	fprintf(f, "\n--- monitors ---\n");
	wl_list_for_each(m, &mons, link) {
		fprintf(f, "monitor: %s\n", m->wlr_output->name);
		fprintf(f, "  identifiers: description=%s make=%s model=%s serial=%s\n",
				m->wlr_output->description ? m->wlr_output->description : "Unknown",
				m->wlr_output->make ? m->wlr_output->make : "Unknown",
				m->wlr_output->model ? m->wlr_output->model : "Unknown",
				m->wlr_output->serial ? m->wlr_output->serial : "Unknown");
		fprintf(f,
				"  phys: width_mm=%.2f height_mm=%.2f origin_x_mm=%.2f origin_y_mm=%.2f "
				"mm_per_px_x=%.2f mm_per_px_y=%.2f width_configured=%d height_configured=%d "
				"origin_configured=%d\n",
				m->phys.width_mm, m->phys.height_mm, m->phys.origin_x_mm, m->phys.origin_y_mm,
				m->phys.mm_per_px_x, m->phys.mm_per_px_y, m->phys.width_configured,
				m->phys.height_configured, m->phys.origin_configured);
		fprintf(f, "  monitor_area: x=%d y=%d w=%d h=%d\n", m->monitor_area.x, m->monitor_area.y,
				m->monitor_area.width, m->monitor_area.height);
		fprintf(f, "  window_area: x=%d y=%d w=%d h=%d\n", m->window_area.x, m->window_area.y,
				m->window_area.width, m->window_area.height);
		fprintf(f, "  focus_vout: %s\n", m->focus_vout ? m->focus_vout->name : "(null)");
		fprintf(f, "  asleep: %d\n", m->asleep);
		fprintf(f, "  enabled: %d\n", m->wlr_output->enabled);

		fprintf(f, "  virtual outputs:\n");
		wl_list_for_each(vout, &m->vouts, link) {
			fprintf(f, "    vout: %s (id=%u)\n", vout->name, vout->id);
			fprintf(f, "      layout_geom: x=%d y=%d w=%d h=%d\n", vout->layout_geom.x, vout->layout_geom.y,
					vout->layout_geom.width, vout->layout_geom.height);
			fprintf(f, "      active_ws: %s (id=%u)\n", vout->ws ? vout->ws->name : "(null)",
					vout->ws ? vout->ws->id : 0);
			fprintf(f, "      mfact: %.2f, nmaster: %d, layout: %s\n", vout->mfact, vout->nmaster,
					vout->ltsymbol);

			fprintf(f, "      workspaces:\n");
			wl_list_for_each(ws, &vout->workspaces, link) {
				fprintf(f, "        ws: %s (id=%u)%s\n", ws->name, ws->id,
						ws == vout->ws ? " [active]" : "");
			}
		}
	}

	fprintf(f, "\n--- clients ---\n");
	wl_list_for_each(c, &clients, link) {
		const char *appid = client_get_appid(c);
		const char *title = client_get_title(c);
		Monitor *cmon = CLIENT_MON(c);
		VirtualOutput *cvout = CLIENT_VOUT(c);

		client_count++;
		fprintf(f, "client %d: appid=\"%s\" title=\"%s\"\n", client_count, appid, title);
		fprintf(f, "  type: %s\n", c->type == XDGShell ? "xdg" : c->type == X11 ? "x11" : "unknown");
		fprintf(f, "  geom: x=%d y=%d w=%d h=%d\n", c->geom.x, c->geom.y, c->geom.width, c->geom.height);
		fprintf(f, "  floating content: w=%d h=%d saved=%d\n", c->float_width, c->float_height,
				c->float_size_saved);
		fprintf(f, "  prev: x=%d y=%d w=%d h=%d\n", c->prev.x, c->prev.y, c->prev.width, c->prev.height);
		fprintf(f, "  mon: %s\n", cmon ? cmon->wlr_output->name : "(null)");
		fprintf(f, "  vout: %s\n", cvout ? cvout->name : "(null)");
		fprintf(f, "  ws: %s (id=%u)\n", c->ws ? c->ws->name : "(null)", c->ws ? c->ws->id : 0);
		fprintf(f, "  isfullscreen: %d (mode=%d), isurgent: %d, isfloating: %d\n", c->isfullscreen,
				c->fullscreen_mode, c->isurgent, c->isfloating);
		fprintf(f, "  bw: %u, resize: %u\n", c->bw, c->resize);
		fprintf(f, "  visible: %d\n", cmon && VISIBLEON(c, cmon));
		fprintf(f, "  scene_node enabled: %d\n", c->scene->node.enabled);
	}
	fprintf(f, "\ntotal clients: %d\n", client_count);

	fprintf(f, "\n--- focus stack (fstack order) ---\n");
	client_count = 0;
	wl_list_for_each(c, &fstack, flink) {
		client_count++;
		fprintf(f, "%d: appid=\"%s\" title=\"%s\" ws=%u\n", client_count, client_get_appid(c),
				client_get_title(c), c->ws ? c->ws->id : 0);
	}

	fclose(f);
	wlr_log(WLR_INFO, "debugstate: wrote /tmp/vwl-debug.txt");
}
