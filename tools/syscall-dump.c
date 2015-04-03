/* 

Compile:
  gcc -o syscall-dump.o -c -fPIC -Wall syscall-dump.c
  gcc -o syscall-dump.so -shared -rdynamic syscall-dump.o -ldl

Usage:
   export LD_PRELOAD=/tmp/syscall-dump.so
   export SYSCALL_DUMP_LOG=/tmp/syscall.log
   ..run.a.binary..

*/

#define _LARGEFILE64_SOURCE
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#if defined(RTLD_NEXT)
#define REAL_LIBC RTLD_NEXT
#else
#define REAL_LIBC ((void *) -1L)
#endif

/* Function pointers for real libc versions */
static int dlog_fd = -1;
static int (*real_open)(const char *pathname, int flags, ...);
static int (*real_open64)(const char *pathname, int flags, ...);
static int (*real_socket)(int domain, int type, int protocol);
static int (*real_ioctl)(int fd, unsigned long request, ...);
static ssize_t (*real_write)(int fd, const void *buf, size_t len);
static ssize_t (*real_read)(int fd, void *buf, size_t len);
static off_t (*real_lseek)(int fd, off_t offset, int whence);
static off64_t (*real_lseek64)(int fd, off64_t offset, int whence);
static int (*real_close)(int fd);
static int (*real_dup)(int oldfd);
static int (*real_dup2)(int oldfd, int newfd);

#define REDIR(realptr, symname) do { \
  if ((realptr) == NULL) { \
    (realptr) = dlsym(REAL_LIBC, symname); \
    if ((realptr) == NULL) exit(1001); \
  } \
} while (0)

#define E(r) (r < 0 ? errno : 0)

/* log function */
static void dlog(const char *fmt, ...)
{
  char buf[2048], *b;
  va_list ap;
  size_t len;
  ssize_t r;
  int keep_errno = errno;

  if (dlog_fd < 0) {
    const char *f = getenv("SYSCALL_DUMP_LOG");
    REDIR(real_open, "open");
    dlog_fd = f ? real_open(f, O_CREAT|O_APPEND|O_WRONLY, 0600) : 2 /* stderr */;
    if (dlog_fd < 0) exit(1002);
  }
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  REDIR(real_write, "write");
  len = strlen(b = buf);
  while (len > 0) {
    r = real_write(dlog_fd, b, len);
    if (r < 0) {
      if (errno != EAGAIN && errno != EINTR && errno != EWOULDBLOCK)
        break;
    } else {
      len -= r;
      b += r;
    }
  }
  errno = keep_errno;
}

static void dlog_hexa(const char *prefix, void *buf, size_t len)
{
  char b[128];
  size_t i, l;
  int keep_errno = errno;
  
  while (len > 0) {
    for (i = l = 0; i < 16 && len > 0; i++, len--, buf++)
      l += sprintf(b + l, "%s%02x", i > 0 ? ":" : "", *(unsigned char *)buf);
    b[l] = '\n';
    b[l+1] = '\0';
    dlog("%s %s", prefix, b);
  }
  errno = keep_errno;
}

/* open() wrapper */
int open(const char *pathname, int flags, ...)
{
  va_list ap;
  mode_t mode;
  int r;
  
  REDIR(real_open, "open");

  if (flags & O_CREAT) {
    /* Get argument */
    va_start(ap, flags);
    mode = va_arg(ap, mode_t);
    va_end(ap);

    r = real_open(pathname, flags, mode);
    dlog("open('%s', 0x%x, 0x%lx) = %d (%d)\n", pathname, flags, (long)mode, r, E(r));
  } else {
    r = real_open(pathname, flags);
    dlog("open('%s', 0x%x) = %d (%d)\n", pathname, flags, r, E(r));
  }
  return r;
}

/* open64() wrapper */
int open64(const char *pathname, int flags, ...)
{
  va_list ap;
  mode_t mode;
  int r;
  
  REDIR(real_open64, "open64");

  if (flags & O_CREAT) {
    /* Get argument */
    va_start(ap, flags);
    mode = va_arg(ap, mode_t);
    va_end(ap);

    r = real_open64(pathname, flags, mode);
    dlog("open64('%s', 0x%x, 0x%lx) = %d (%d)\n", pathname, flags, (long)mode, r, E(r));
  } else {
    r = real_open64(pathname, flags);
    dlog("open64('%s', 0x%x) = %d (%d)\n", pathname, flags, r, E(r));
  }
  return r;
}

/* socket() wrapper */
int socket(int domain, int type, int protocol)
{
  int r;

  REDIR(real_socket, "socket");

  r = real_socket(domain, type, protocol);
  dlog("socket(%d, %d, %d) = %d (%d)\n", domain, type, protocol, r, E(r));
  return r;
}

/* close() wrapper */
int close(int fd)
{
  int r;

  REDIR(real_close, "close");

  r = real_close(fd);
  dlog("close(%d) = %d (%d)\n", fd, r, E(r));
  return r;
}

/* write() wrapper */
ssize_t write(int fd, const void *buf, size_t len)
{
  int r;

  REDIR(real_write, "write");
  
  r = real_write(fd, buf, len);
  if (r > 0) {
    dlog_hexa("write:", (void *)buf, r);
    dlog("  write(%d, %p, %zd) = %d (%d)\n", fd, buf, len, r, E(r));
  } else {
    dlog("write(%d, %p, %zd) = %d (%d)\n", fd, buf, len, r, E(r));
  }
  return r;
}

/* read() wrapper */
ssize_t read(int fd, void *buf, size_t len)
{
  int r;

  REDIR(real_read, "read");
  
  r = real_read(fd, buf, len);
  dlog("read(%d, %p, %zd) = %d (%d)\n", fd, buf, len, r, E(r));
  if (r > 0)
    dlog_hexa("  read:", buf, r);
  return r;
}

/* lseek() wrapper */
off_t lseek(int fd, off_t offset, int whence)
{
  int r;

  REDIR(real_lseek, "lseek");

  r = real_lseek(fd, offset, whence);
  dlog("lseek(%d, %ld, %d) = %d (%d)\n", fd, (long)offset, whence, r, E(r));
  return r;
}

/* lseek64() wrapper */
off64_t lseek64(int fd, off64_t offset, int whence)
{
  int r;

  REDIR(real_lseek64, "lseek64");

  r = real_lseek64(fd, offset, whence);
  dlog("lseek(%d, %lld, %d) = %d (%d)\n", fd, (long long)offset, whence, r, E(r));
  return r;
}

/* dup() wrapper */
int dup(int oldfd)
{
  int r;

  REDIR(real_dup, "dup");

  r = real_dup(oldfd);
  dlog("dup(%d) = %d (%d)\n", oldfd, r, E(r));
  return r;
}

/* dup2() wrapper */
int dup2(int oldfd, int newfd)
{
  int r;

  REDIR(real_dup2, "dup2");

  r = real_dup2(oldfd, newfd);
  dlog("dup2(%d, %d) = %d (%d)\n", oldfd, newfd, r, E(r));
  return r;
}

/* ioctl() wrapper */
int ioctl(int fd, unsigned long request, ...)
{
  va_list ap;
  unsigned long size, dir, arg;
  int r;

  REDIR(real_ioctl, "ioctl");

  /* Get argument */
  va_start(ap, request);
  arg = va_arg(ap, unsigned long);
  va_end(ap);

//  nr = (request >> _IOC_NRSHIFT) & _IOC_NRMASK;
  size = (request >> _IOC_SIZESHIFT) & _IOC_SIZEMASK;
//type = (request >> _IOC_TYPESHIFT) & _IOC_TYPEMASK;
   dir = (request >> _IOC_DIRSHIFT) & _IOC_DIRMASK;

  switch (dir) {
  case _IOC_NONE:
    r = real_ioctl(fd, request, arg);
    dlog("ioctl(%d, 0x%04lx, 0x%08x) = %d (%d)\n", fd, request, arg, r, E(r));
    break;
  case _IOC_READ:
    r = real_ioctl(fd, request, arg);
    dlog("ioctl(%d, 0x%04lx, %p) = %d (%d)\n", fd, request, arg, r, E(r));
    dlog_hexa("  ioctl()/r:", (void *)arg, size);
    break;
  case _IOC_WRITE:
    dlog_hexa("ioctl()/w:", (void *)arg, size);
    r = real_ioctl(fd, request, arg);
    dlog("  ioctl(%d, 0x%04lx, %p) = %d (%d)\n", fd, request, arg, r, E(r));
    break;
  case _IOC_READ|_IOC_WRITE:
    dlog_hexa("ioctl()/w:", (void *)arg, size);
    r = real_ioctl(fd, request, arg);
    dlog("  ioctl(%d, 0x%04lx, %p) = %d (%d)\n", fd, request, arg, r, E(r));
    dlog_hexa("  ioctl()/r:", (void *)arg, size);
    break;
  }

  return r;
}