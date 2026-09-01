#ifndef SPAWNRULES_H
#define SPAWNRULES_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Client Client;
struct wlr_xdg_activation_token_v1;
struct wlr_xdg_activation_v1;

void spawnrules_init(struct wlr_xdg_activation_v1 *activation);
void spawnrules_finish(void);
bool spawnrules_apply(Client *c);
bool spawnrules_bind_token(Client *c, struct wlr_xdg_activation_token_v1 *token);
bool spawnrules_track_startup_id(uint32_t x11_window, uint32_t x11_group, const char *startup_id);
void spawnrules_forget(Client *c);
int spawnrules_spawn_on_workspace_argv(unsigned int workspace_id, const char *const argv[]);
int ipc_spawn_on_workspace(unsigned int workspace_id, const char *command);

#endif
