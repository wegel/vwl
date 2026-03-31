#ifndef SPAWNRULES_H
#define SPAWNRULES_H

#include <stdbool.h>

typedef struct Client Client;

void spawnrules_init(void);
void spawnrules_finish(void);
bool spawnrules_apply(Client *c);
int spawnrules_spawn_on_workspace_argv(unsigned int workspace_id, const char *const argv[]);
int ipc_spawn_on_workspace(unsigned int workspace_id, const char *command);

#endif
