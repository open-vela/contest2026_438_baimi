#ifndef _UNISTD_H
#define _UNISTD_H

int usleep(unsigned int usec);
long read(int fd, void* buf, unsigned long count);
long write(int fd, const void* buf, unsigned long count);
int close(int fd);
int unlink(const char* path);

#endif
