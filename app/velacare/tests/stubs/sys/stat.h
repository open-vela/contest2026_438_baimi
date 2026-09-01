#ifndef _SYS_STAT_H
#define _SYS_STAT_H

struct stat
{
  long st_size;
  unsigned int st_mode;
};

#define S_ISDIR(m) (((m) & 0170000) == 0040000)

int stat(const char* path, struct stat* buf);
int mkdir(const char* path, unsigned int mode);

#endif
