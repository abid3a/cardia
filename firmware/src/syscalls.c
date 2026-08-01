/* syscalls.c -- newlib's OS hooks, deliberately made fatal.
 *
 * The link pulls in -specs=nosys.specs, which supplies these as stubs that
 * return -1 and set errno. That is the wrong behaviour for this program.
 *
 * Cardia is built on a hard rule: no dynamic allocation anywhere, ever. The
 * linker script reserves a zero-byte heap to back that up statically. These
 * definitions back it up dynamically. If anything ever calls malloc -- a
 * refactor that innocently uses strdup, a library added later, a printf that
 * slips in through a debug line -- _sbrk is where it lands, and the choice is
 * between:
 *
 *   (a) returning an error, so malloc returns NULL, so the caller either
 *       handles it (unlikely, since it did not know it was allocating) or
 *       dereferences NULL somewhere far from the cause; or
 *   (b) stopping dead, right here, with the call site one frame up the stack.
 *
 * (b) is strictly better on a device that is supposed to run unattended for
 * days. A trap during bring-up is a five-minute fix; a NULL dereference three
 * hours into a recording is a bad afternoon. The same reasoning applies to the
 * file-descriptor calls: nothing in this firmware has a filesystem, so reaching
 * one of them means something is very confused about where it is running.
 *
 * These definitions live in an object file, so the linker prefers them over the
 * identically-named members of libnosys.a, which it then never extracts.
 */

#include <stdint.h>

/* Symbols newlib expects. Only referenced so the ABI matches; nothing here
 * uses them. */
extern int errno;

static void syscall_trap(void)
{
    /* Not an assert, not a return: spin with the caller's frame intact so a
     * debugger attaching afterwards can walk straight back to the offending
     * call. On a board with no console this is also visibly different from
     * normal operation -- LD2 stops toggling. */
    for (;;) {
    }
}

void *_sbrk(int incr)
{
    (void)incr;
    syscall_trap();
    return (void *)-1;
}

int _write(int file, char *ptr, int len)
{
    /* Output goes through the USART driver directly, never through stdio. */
    (void)file;
    (void)ptr;
    (void)len;
    syscall_trap();
    return -1;
}

int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    syscall_trap();
    return -1;
}

int _close(int file)
{
    (void)file;
    syscall_trap();
    return -1;
}

int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    syscall_trap();
    return -1;
}

int _fstat(int file, void *st)
{
    (void)file;
    (void)st;
    syscall_trap();
    return -1;
}

int _isatty(int file)
{
    (void)file;
    syscall_trap();
    return 0;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    syscall_trap();
    return -1;
}

void _exit(int status)
{
    (void)status;
    /* main() never returns, so arriving here means something called exit() or
     * abort(). Either way there is nowhere to go. */
    for (;;) {
    }
}
