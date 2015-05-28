#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

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

__attribute__((__constructor__))
static void record_init() {
  struct utsname u;
  uname(&u);

  char log_name[PATH_MAX];
  snprintf(log_name, PATH_MAX, "%s.%d.memory.log_file", u.nodename, getpid());
  log_file = fopen(log_name, "w");
}

__attribute__((__destructor__))
static void record_cleanup() {
  if (!log_file)
    return;

  (void) fflush(log_file);
  (void) fclose(log_file);
}

static void print_context(unw_context_t *context) {
  unw_cursor_t cursor;
  if (unw_init_local(&cursor, context))
    return;

  while (unw_step(&cursor) > 0) {
    unw_proc_info_t pip;
    if (unw_get_proc_info(&cursor, &pip))
      return;

    unw_word_t off;
    char proc_name[PATH_MAX];
    if (unw_get_proc_name(&cursor, proc_name, PATH_MAX, &off)) {
      off = 0;
      strcpy(proc_name, "?");
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
  }
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

// The malloc hook might use functions that call malloc, and we need to make
// sure this does not cause an infinite loop.
static __thread int in_malloc = 0;

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

