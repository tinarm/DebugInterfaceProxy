
#ifndef MLDPROC_H
#define MLDPROC_H

int mldproc_start(const char *name, const char *cmd);
int mldproc_stop(const char *name);
int mldproc_query(char *resp, uint32_t len);

#endif
