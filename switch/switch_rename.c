/* rename() with POSIX replace semantics on Nintendo Switch.
 *
 * POSIX rename() replaces an existing destination file. Horizon's filesystem
 * refuses instead ("File exists"), so every write-to-temp-then-rename in the
 * game -- the X2/X3 source caches, mod state.toml, saves, settings -- failed
 * the second time it ran. A failed armor cache stops the game on purpose (a
 * selected feature must not silently turn into vanilla gameplay), which
 * looked like a crash on Play from the second launch on.
 *
 * Linked with -Wl,--wrap=rename, so this also covers the libstdc++
 * std::filesystem::rename used by the C++ sources. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int __real_rename(const char *from, const char *to);

int __wrap_rename(const char *from, const char *to) {
  if (__real_rename(from, to) == 0) return 0;
  const int saved = errno;
  struct stat src, dst;
  if (from && to && strcmp(from, to) != 0 &&
      stat(from, &src) == 0 && S_ISREG(src.st_mode) &&
      stat(to, &dst) == 0 && S_ISREG(dst.st_mode) &&
      remove(to) == 0)
    return __real_rename(from, to);
  errno = saved;
  return -1;
}
