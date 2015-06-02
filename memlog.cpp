#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>

// NOTE: This source makes very minimal use of C++11 features. It can still be
// compiled by g++ 4.4.7 with -std=gnu++0x.
#include <unordered_map>
#include <utility>

#include <limits.h>
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

#ifdef __bgq__
#include <spi/include/kernel/location.h>
#include <spi/include/kernel/memory.h>
#endif

using namespace std;

// NOTE: When static linking, this depends on linker wrapping.
// Add to your LDFLAGS:
//   -Wl,--wrap,malloc,--wrap,free,--wrap,realloc,--wrap,calloc,--wrap,memalign /path/to/memlog_s.o -lpthread -ldl

FILE *log_file = 0;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// The malloc hook might use functions that call malloc, and we need to make
// sure this does not cause an infinite loop.
static __thread int in_malloc = 0;
static char self_path[PATH_MAX+1] = { '\0' };

#ifdef __bgq__
int on_bgq = 0;
#endif

void *initial_brk = 0;

__attribute__((__constructor__))
static void record_init() {
  struct utsname u;
  uname(&u);

  int id = (int) getpid();
#ifdef __bgq__
  // If we're really running on a BG/Q compute node, use the job rank instead
  // of the pid because the node name might not really be globally unique.
  if (!strcmp(u.sysname, "CNK") && !strcmp(u.machine, "BGQ")) {
    id = (int) Kernel_GetRank();
    on_bgq = 1;
  }
#endif

  // If we're running under a common batch system, add the job id to the output
  // file names (add it as a prefix so that sorting the files will sort by job
  // first).
  char *job_id = 0;
  const char *job_id_vars[] =
    { "COBALT_JOBID", "PBS_JOBID", "SLURM_JOB_ID", "JOB_ID" };
  for (int i = 0; i < sizeof(job_id_vars)/sizeof(job_id_vars[0]); ++i) {
    job_id = getenv(job_id_vars[i]);
    if (job_id)
      break;
  }

  char log_name[PATH_MAX+1];
  if (job_id)
    snprintf(log_name, PATH_MAX+1, "%s.%s.%d.memlog", job_id, u.nodename, id);
  else
    snprintf(log_name, PATH_MAX+1, "%s.%d.memlog", u.nodename, id);
  log_file = fopen(log_name, "w");
  if (!log_file)
    fprintf(stderr, "fopen failed for '%s': %m\n", log_name);

  const char *link_name = "/proc/self/exe";
  readlink(link_name, self_path, PATH_MAX);

  initial_brk = sbrk(0);
}

__attribute__((__destructor__))
static void record_cleanup() {
  if (!log_file)
    return;

  // These functions might call free, but we're shutting down, so don't try to
  // unwind the stack from here...
  in_malloc = 1;

  // Avoid any racing by obtaining the lock.
  if (pthread_mutex_lock(&log_mutex))
    return;

  (void) fflush(log_file);
  (void) fclose(log_file);
}

// dladdr is, relatively, quit slow. For this to work on a large application,
// we need to cache the lookup results.
static int dladdr_cached(void * addr, Dl_info *info) {
  static unordered_map<void *, Dl_info> dladdr_cache;

  auto I = dladdr_cache.find(addr);
  if (I == dladdr_cache.end()) {
    int r;
    if (!(r = dladdr(addr, info)))
      memset(info, 0, sizeof(Dl_info));

    dladdr_cache.insert(make_pair(addr, *info));
    return r;
  }

  memcpy(info, &I->second, sizeof(Dl_info));
  return 1;
}

static void print_context(const void *caller, int show_backtrace) {
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage)) {
    fprintf(stderr, "getrusage failed: %m\n");
    return;
  }

  fprintf(log_file, "\t%ld.%06ld %ld %ld", usage.ru_utime.tv_sec,
          usage.ru_utime.tv_usec, usage.ru_maxrss, syscall(SYS_gettid));

  // Some other memory stats (like with maxrss, report these in KB).
  size_t arena_size = ((size_t) sbrk(0)) - (size_t) initial_brk;

  uint64_t mmap_size = 0;
#ifdef __bgq__
  if (on_bgq)
    (void) Kernel_GetMemorySize(KERNEL_MEMSIZE_MMAP, &mmap_size);
#endif

  fprintf(log_file, " %ld %ld", arena_size >> 10, mmap_size >> 10);

  if (!show_backtrace)
    return;

  void *pcs[1024];
  int num_pcs = backtrace(pcs, 1024);

  int found_caller = 0;
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
    if (dladdr_cached((void *) pc, &dlinfo) && dlinfo.dli_fname &&
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
extern "C" {
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
  const void *caller =
    __builtin_extract_return_addr(__builtin_return_address(0));

  if (in_malloc)
    return __libc_malloc(size);

  in_malloc = 1;

  void *ptr = __libc_malloc(size);

  record_malloc(size, ptr, caller);

  in_malloc = 0;
  return ptr;
}

void *FUNC(realloc)(void *ptr, size_t size) {
  const void *caller =
    __builtin_extract_return_addr(__builtin_return_address(0));

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
  const void *caller =
    __builtin_extract_return_addr(__builtin_return_address(0));

  if (in_malloc)
    return __libc_calloc(nmemb, size);

  in_malloc = 1;

  void *ptr = __libc_calloc(nmemb, size);

  record_malloc(nmemb*size, ptr, caller);

  in_malloc = 0;

  return ptr;
}

void *FUNC(memalign)(size_t boundary, size_t size) {
  const void *caller =
    __builtin_extract_return_addr(__builtin_return_address(0));

  if (in_malloc)
    return __libc_memalign(boundary, size);

  in_malloc = 1;

  void *ptr = __libc_memalign(boundary, size);

  record_malloc(size, ptr, caller);

  in_malloc = 0;

  return ptr;
}

void FUNC(free)(void *ptr) {
  const void *caller =
    __builtin_extract_return_addr(__builtin_return_address(0));

  if (in_malloc || !ptr)
    return __libc_free(ptr);

  in_malloc = 1;

  record_free(ptr, caller);

  __libc_free(ptr);

  in_malloc = 0;
}

} // extern "C"

