/* SPDX-License-Identifier: MIT
 * Opt-in native DRM calls for a glibc RADV build hosted by Proroot.
 * Only the real Xclipse render node and DRM ioctl class are routed directly.
 * Android's ordinary process permissions and SELinux checks still apply.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int __real_open(const char *, int, ...);
extern int __real_open64(const char *, int, ...);
extern int __real_ioctl(int, unsigned long, ...);
static int enabled(void) {
    const char *value = getenv("PROROOT_NATIVE_DRM");
    return value && !strcmp(value,"1");
}
#ifdef __aarch64__
static long kernel_call(long number,long a,long b,long c,long d) {
    register long x0 __asm__("x0")=a;
    register long x1 __asm__("x1")=b;
    register long x2 __asm__("x2")=c;
    register long x3 __asm__("x3")=d;
    register long x8 __asm__("x8")=number;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x1),"r"(x2),"r"(x3),"r"(x8) : "memory","cc");
    if(x0<0 && x0>=-4095){errno=(int)-x0;return -1;}
    return x0;
}
#endif
static int native_open(const char *path,int flags,unsigned int mode) {
#ifdef __aarch64__
    if(enabled() && path && !strcmp(path,"/dev/dri/renderD128"))
        return (int)kernel_call(56,AT_FDCWD,(long)path,flags,mode);
#endif
    return __real_open(path,flags,mode);
}
int __wrap_open(const char *path,int flags,...) {
    unsigned int mode=0;
    if((flags&O_CREAT)||((flags&O_TMPFILE)==O_TMPFILE)){va_list ap;va_start(ap,flags);mode=va_arg(ap,unsigned int);va_end(ap);}
    return native_open(path,flags,mode);
}
int __wrap_open64(const char *path,int flags,...) {
    unsigned int mode=0;
    if((flags&O_CREAT)||((flags&O_TMPFILE)==O_TMPFILE)){va_list ap;va_start(ap,flags);mode=va_arg(ap,unsigned int);va_end(ap);}
    return native_open(path,flags,mode);
}
int __wrap_ioctl(int fd,unsigned long request,...) {
    va_list ap;va_start(ap,request);void *arg=va_arg(ap,void *);va_end(ap);
#ifdef __aarch64__
    if(enabled() && ((request>>8)&0xff)=='d')
        return (int)kernel_call(29,fd,(long)request,(long)arg,0);
#endif
    return __real_ioctl(fd,request,arg);
}
