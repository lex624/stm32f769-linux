/* Single-threaded errno for /bin/lsh (see build-lsh.sh).
 *
 * The toolchain's uClibc is built with linuxthreads, so its __errno_location
 * looks up the calling thread, and that one reference links the whole thread
 * manager: 16 KB of thread handles, 8 KB of keys and the manager code. lsh
 * never creates threads, so a plain process-wide errno is correct, and
 * defining these first keeps the linuxthreads errno.o (and everything it
 * pulls in) out of the link. Both the public names and uClibc's internal
 * __GI_ aliases are defined because libc objects call the aliases. */
#include <signal.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static int lsh_errno, lsh_h_errno;

/* abort() calls raise(), whose linuxthreads version pulls in the same
 * manager. For one thread, raise is kill(getpid()). */
int raise(int sig) { return kill(getpid(), sig); }
int __GI_raise(int sig) __attribute__((alias("raise")));

/* sleep() calls nanosleep, whose linuxthreads version is a cancellation
 * point wrapper (wrapsyscall.o) that pulls in the manager too. */
int nanosleep(const struct timespec *req, struct timespec *rem)
{
    return (int)syscall(__NR_nanosleep, req, rem);
}
int __GI_nanosleep(const struct timespec *, struct timespec *) __attribute__((alias("nanosleep")));

int *__errno_location(void) { return &lsh_errno; }
int *__h_errno_location(void) { return &lsh_h_errno; }
int *__GI___errno_location(void) __attribute__((alias("__errno_location")));
int *__GI___h_errno_location(void) __attribute__((alias("__h_errno_location")));
