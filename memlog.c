#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

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

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include <dlfcn.h>

FILE *log_file = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// The malloc hook might use functions that call malloc, and we need to make
// sure this does not cause an infinite loop.
static __thread int in_malloc = 0;

__attribute__((__constructor__))
static void record_init() {
  struct utsname u;
  uname(&u);

  char log_name[PATH_MAX];
  snprintf(log_name, PATH_MAX, "%s.%d.memory.log_file", u.nodename, getpid());
  log_file = fopen(log_name, "w");
  if (!log_file)
    fprintf(stderr, "fopen failed for '%s': %m\n", log_name);
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

static void print_context(unw_context_t *context) {
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage)) {
    fprintf(stderr, "getrusage failed: %m\n");
    return;
  }

  fprintf(log_file, "\t%ld.%06ld %ld %ld", usage.ru_utime.tv_sec,
          usage.ru_utime.tv_usec, usage.ru_maxrss, syscall(SYS_gettid));

  int r;
  unw_cursor_t cursor;
  if ((r = unw_init_local(&cursor, context))) {
    fprintf(stderr, "unw_init_local failed: %s [%d]\n",
            unw_strerror(r), r);
    return;
  }

  void *pcs[1024];
  int num_pcs = backtrace(pcs, 1024);
  for (int pci = 0; pci < num_pcs; ++pci) {
    unw_word_t pc = (unw_word_t) pcs[pci];
#if 0
  while ((r = unw_step(&cursor)) > 0) {
    unw_word_t pc;
    if ((r = unw_get_reg(&cursor, UNW_REG_IP, &pc))) {
      fprintf(stderr, "unw_get_reg UNW_REG_IP failed: %s [%d]\n",
              unw_strerror(r), r);
      return;
    }
#endif

    if (!pc)
      break;

    unw_word_t off, relpc;
    const char *proc_name;
    const char *file_name;
    Dl_info dlinfo;
    if (dladdr((void *) pc, &dlinfo) && dlinfo.dli_fname &&
        *dlinfo.dli_fname) {
      unw_word_t saddr = (unw_word_t) dlinfo.dli_saddr;
      if (saddr) {
#if defined(__powerpc64__) && !defined(__powerpc64le__)
        // On PPC64 ELFv1, the symbol address points to the function descriptor, not
        // the actual starting address.
        saddr = *(unw_word_t*) saddr;
#endif

        off = pc - saddr;
        relpc = pc - ((unw_word_t) dlinfo.dli_fbase);
      } else {
        off = 0;
        relpc = 0;
      }

      proc_name = dlinfo.dli_sname;
      if (!proc_name)
        proc_name = "?";

      file_name = dlinfo.dli_fname;
    } else {
      off = pc;
      relpc = pc;
      proc_name = "?";
      file_name = "?";
    }

    fprintf(log_file, "\t%s (%s+0x%x) [0x%lx (0x%lx)]", file_name, proc_name, (int) off,
            (long) pc, (long) relpc);

#if 0
    unw_word_t off;
    char proc_name[PATH_MAX];
    if (unw_get_proc_name(&cursor, proc_name, PATH_MAX, &off)) {
      off = 0;
      strcpy(proc_name, "?");
    }

    unw_proc_info_t pip;
    if ((r = unw_get_proc_info(&cursor, &pip))) {
      // unw_get_proc_info is not supported on some platforms (ppc and ppc64,
      // for example), so we need to try harder...
      if (r == -UNW_EINVAL) {
        unw_word_t pc;
        if ((r = unw_get_reg(&cursor, UNW_REG_IP, &pc))) {
          fprintf(stderr, "unw_get_reg UNW_REG_IP failed: %s [%d]\n",
                  unw_strerror(r), r);
          return;
        }

        if ((r = unw_get_proc_info_by_ip(unw_local_addr_space, pc, &pip, NULL))) {
          if (r == -UNW_ENOINFO)
            break; // the cursor is now invalid; must break here.

          fprintf(stderr, "unw_get_proc_info_by_ip failed: %s [%d]\n",
                  unw_strerror(r), r);
          return;
        }
      } else {
        if (r == -UNW_ENOINFO)
          break; // the cursor is now invalid; must break here.

        fprintf(stderr, "unw_get_proc_info failed: %s [%d]\n",
                unw_strerror(r), r);
        return;
      }
    }

    const char *file_name;
    Dl_info dlinfo;
    if (dladdr((void *)(pip.start_ip + off), &dlinfo) && dlinfo.dli_fname &&
        *dlinfo.dli_fname)
      file_name = dlinfo.dli_fname;
    else
      file_name = "?";

    fprintf(log_file, "\t%s (%s+0x%x) [%p]", file_name, proc_name, (int) off,
            (void *) (pip.start_ip + off));
#endif
  }

  if (r < 0 && r != -UNW_ENOINFO)
    fprintf(stderr, "unw_step failed: %s [%d]\n",
            unw_strerror(r), r);
}

static void record_malloc(size_t size, void *ptr, unw_context_t *uc) {
  if (!log_file)
    return;

  if (pthread_mutex_lock(&log_mutex))
    return;

  fprintf(log_file, "M: %zd %p", size, ptr);
  print_context(uc);
  fprintf(log_file, "\n");

done:
  pthread_mutex_unlock(&log_mutex);
}

static void record_free(void *ptr, unw_context_t *uc) {
  if (!log_file)
    return;

  if (pthread_mutex_lock(&log_mutex))
    return;

  fprintf(log_file, "F: %p", ptr);
  print_context(uc);
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

void *malloc(size_t size) {
  if (in_malloc)
    return __libc_malloc(size);

  in_malloc = 1;

  void *ptr = __libc_malloc(size);

  unw_context_t uc;
  if (!unw_getcontext(&uc))
    record_malloc(size, ptr, &uc);

  in_malloc = 0;
  return ptr;
}

void *realloc(void *ptr, size_t size) {
  if (in_malloc)
    return __libc_realloc(ptr, size);

  in_malloc = 1;

  void *nptr = __libc_realloc(ptr, size);

  unw_context_t uc;
  if (!unw_getcontext(&uc)) {
    record_free(ptr, &uc);
    record_malloc(size, nptr, &uc);
  }

  in_malloc = 0;

  return nptr;
}

void *calloc(size_t nmemb, size_t size) {
  if (in_malloc)
    return __libc_calloc(nmemb, size);

  in_malloc = 1;

  void *ptr = __libc_calloc(nmemb, size);

  unw_context_t uc;
  if (!unw_getcontext(&uc))
    record_malloc(nmemb*size, ptr, &uc);

  in_malloc = 0;

  return ptr;
}

void *memalign(size_t boundary, size_t size) {
  if (in_malloc)
    return __libc_memalign(boundary, size);

  in_malloc = 1;

  void *ptr = __libc_memalign(boundary, size);

  unw_context_t uc;
  if (!unw_getcontext(&uc))
    record_malloc(size, ptr, &uc);

  in_malloc = 0;

  return ptr;
}

void free(void *ptr) {
  if (in_malloc)
    return __libc_free(ptr);

  in_malloc = 1;

  unw_context_t uc;
  if (!unw_getcontext(&uc))
    record_free(ptr, &uc);

  __libc_free(ptr);

  in_malloc = 0;
}

