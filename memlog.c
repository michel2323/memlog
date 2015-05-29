#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

#include <malloc.h>
#include <execinfo.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <unistd.h>

#include <pthread.h>

#include <dlfcn.h>

// NOTE: When static linking, this depends on linker wrapping.
// Add to your LDFLAGS:
//   -Wl,--wrap,malloc,--wrap,free,--wrap,realloc,--wrap,calloc,--wrap,memalign /path/to/memlog_s.o -lpthread -ldl

FILE *log_file = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// The malloc hook might use functions that call malloc, and we need to make
// sure this does not cause an infinite loop.
static __thread int in_malloc = 0;
static char self_path[PATH_MAX+1] = { '\0' };

__attribute__((__constructor__))
static void record_init() {
  struct utsname u;
  uname(&u);

  char log_name[PATH_MAX+1];
  snprintf(log_name, PATH_MAX+1, "%s.%d.memlog", u.nodename, getpid());
  log_file = fopen(log_name, "w");
  if (!log_file)
    fprintf(stderr, "fopen failed for '%s': %m\n", log_name);

  const char *link_name = "/proc/self/exe";
  readlink(link_name, self_path, PATH_MAX);
}

__attribute__((__destructor__))
static void record_cleanup() {
  if (!log_file)
    return;

  // These functions might call free, but we're shutting down, so don't try to
  // unwind the stack from here...
  in_malloc = 1;

  (void) fflush(log_file);
  (void) fclose(log_file);
}

static void print_context(const void *caller, int show_backtrace) {
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage)) {
    fprintf(stderr, "getrusage failed: %m\n");
    return;
  }

  fprintf(log_file, "\t%ld.%06ld %ld %ld", usage.ru_utime.tv_sec,
          usage.ru_utime.tv_usec, usage.ru_maxrss, syscall(SYS_gettid));

  if (!show_backtrace)
    return;

  void *pcs[1024];
  int num_pcs = backtrace(pcs, 1024);

  int found_caller = 0;
  caller =  __builtin_extract_return_addr((void*) caller);
  for (int pci = 0; pci < num_pcs; ++pci) {
    intptr_t pc = (intptr_t) pcs[pci];

    if (!pc)
      break;

    if (!found_caller) {
      if (pc != (intptr_t) caller)
        continue;

      found_caller = 1;
    }

    intptr_t off, relpc;
    const char *proc_name;
    const char *file_name;
    Dl_info dlinfo;
    if (dladdr((void *) pc, &dlinfo) && dlinfo.dli_fname &&
        *dlinfo.dli_fname) {
      intptr_t saddr = (intptr_t) dlinfo.dli_saddr;
      if (saddr) {
#if defined(__powerpc64__) && !defined(__powerpc64le__)
        // On PPC64 ELFv1, the symbol address points to the function descriptor, not
        // the actual starting address.
        saddr = *(intptr_t*) saddr;
#endif

        off = pc - saddr;
        relpc = pc - ((intptr_t) dlinfo.dli_fbase);
      } else {
        off = 0;
        relpc = 0;
      }

      proc_name = dlinfo.dli_sname;
      if (!proc_name)
        proc_name = "?";

      file_name = dlinfo.dli_fname;
    } else {
      // We don't know these...
      off = 0;
      relpc = 0;
      proc_name = "?";

      // If we can't determine the file, assume it is the base executable
      // (which does the right thing for statically-linked binaries).
      file_name = self_path;
    }

    fprintf(log_file, "\t%s (%s+0x%x) [0x%lx (0x%lx)]", file_name, proc_name, (int) off,
            (long) pc, (long) relpc);
  }
}

static void record_malloc(size_t size, void *ptr, const void *caller) {
  if (!log_file)
    return;

  if (pthread_mutex_lock(&log_mutex))
    return;

  fprintf(log_file, "M: %zd %p", size, ptr);
  print_context(caller, 1);
  fprintf(log_file, "\n");

done:
  pthread_mutex_unlock(&log_mutex);
}

static void record_free(void *ptr, const void *caller) {
  if (!log_file)
    return;

  if (pthread_mutex_lock(&log_mutex))
    return;

  fprintf(log_file, "F: %p", ptr);
  print_context(caller, 0);
  fprintf(log_file, "\n");

done:
  pthread_mutex_unlock(&log_mutex);
}

// glibc exports its underlying malloc implementation under the name
// __libc_malloc so that hooks like this can use it.
extern void *__libc_malloc(size_t size);
extern void *__libc_realloc(void *ptr, size_t size);
extern void *__libc_calloc(size_t nmemb, size_t size);
extern void *__libc_memalign(size_t boundary, size_t size);
extern void __libc_free(void *ptr);

#ifdef __PIC__
#define FUNC(x) x
#else
#define FUNC(x) __wrap_ ## x
#endif

void *FUNC(malloc)(size_t size) {
  const void *caller = __builtin_return_address(0);

  if (in_malloc)
    return __libc_malloc(size);

  in_malloc = 1;

  void *ptr = __libc_malloc(size);

  record_malloc(size, ptr, caller);

  in_malloc = 0;
  return ptr;
}

void *FUNC(realloc)(void *ptr, size_t size) {
  const void *caller = __builtin_return_address(0);

  if (in_malloc)
    return __libc_realloc(ptr, size);

  in_malloc = 1;

  void *nptr = __libc_realloc(ptr, size);

  if (ptr)
    record_free(ptr, caller);
  record_malloc(size, nptr, caller);

  in_malloc = 0;

  return nptr;
}

void *FUNC(calloc)(size_t nmemb, size_t size) {
  const void *caller = __builtin_return_address(0);

  if (in_malloc)
    return __libc_calloc(nmemb, size);

  in_malloc = 1;

  void *ptr = __libc_calloc(nmemb, size);

  record_malloc(nmemb*size, ptr, caller);

  in_malloc = 0;

  return ptr;
}

void *FUNC(memalign)(size_t boundary, size_t size) {
  const void *caller = __builtin_return_address(0);

  if (in_malloc)
    return __libc_memalign(boundary, size);

  in_malloc = 1;

  void *ptr = __libc_memalign(boundary, size);

  record_malloc(size, ptr, caller);

  in_malloc = 0;

  return ptr;
}

void FUNC(free)(void *ptr) {
  const void *caller = __builtin_return_address(0);

  if (in_malloc || !ptr)
    return __libc_free(ptr);

  in_malloc = 1;

  record_free(ptr, caller);

  __libc_free(ptr);

  in_malloc = 0;
}

