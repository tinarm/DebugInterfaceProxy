
#ifndef CMDSERVER_H
#define CMDSERVER_H

int cmdserver_start(const char *port);
void cmdserver_wait(void);
void cmdserver_closefd(void);

#endif
