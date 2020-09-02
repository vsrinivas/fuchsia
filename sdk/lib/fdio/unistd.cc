// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "unistd.h"

#include <dirent.h>
#include <fcntl.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/private.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/vfs.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <threads.h>
#include <utime.h>
#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <cstdarg>
#include <ctime>

#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "private.h"

namespace fio = ::llcpp::fuchsia::io;
namespace fsocket = ::llcpp::fuchsia::posix::socket;

static_assert(IOFLAG_CLOEXEC == FD_CLOEXEC, "Unexpected fdio flags value");

// Helper functions

// Open |path| from the |dirfd| directory, enforcing the POSIX EISDIR error condition. Specifically,
// ZX_ERR_NOT_FILE will be returned when opening a directory with write access/O_CREAT.
static zx_status_t __fdio_open_at(fdio_t** io, int dirfd, const char* path, int flags,
                                  uint32_t mode);
// Open |path| from the |dirfd| directory, but allow creating directories/opening them with
// write access. Note that this differs from POSIX behavior.
static zx_status_t __fdio_open_at_ignore_eisdir(fdio_t** io, int dirfd, const char* path, int flags,
                                                uint32_t mode);
// Open |path| from the current working directory, respecting EISDIR.
static zx_status_t __fdio_open(fdio_t** io, const char* path, int flags, uint32_t mode);

// non-thread-safe emulation of unistd io functions
// using the fdio transports

// Constexpr function to compute the initialized value for __fdio_globbal_state at compile time.
// If this is not constexpr, then initialization would happen after __libc_extensions_init,
// wiping the fd table *after* it has been fill in with valid entries.
// (musl invokes "__libc_start_init" after "__libc_extensions_init")
static constexpr fdio_state_t initialize_fdio_state() {
  fdio_state_t state = {};
  state.lock = MTX_INIT;
  state.cwd_lock = MTX_INIT;
  state.cwd_path[0] = '/';
  state.cwd_path[1] = '\0';
  return state;
}
fdio_state_t __fdio_global_state = initialize_fdio_state();

static bool fdio_is_reserved_or_null(fdio_t* io) {
  if (io == nullptr || io == fdio_get_reserved_io()) {
    return true;
  }
  return false;
}

int fdio_reserve_fd(int starting_fd) {
  if ((starting_fd < 0) || (starting_fd >= FDIO_MAX_FD)) {
    errno = EINVAL;
    return -1;
  }
  fbl::AutoLock lock(&fdio_lock);
  for (int fd = starting_fd; fd < FDIO_MAX_FD; fd++) {
    if (fdio_fdtab[fd] == nullptr) {
      fdio_fdtab[fd] = fdio_get_reserved_io();
      return fd;
    }
  }
  errno = EMFILE;
  return -1;
}

int fdio_assign_reserved(int fd, fdio_t* io) {
  fbl::AutoLock lock(&fdio_lock);
  fdio_t* res = fdio_fdtab[fd];
  if (res != fdio_get_reserved_io()) {
    errno = EINVAL;
    return -1;
  }
  fdio_dupcount_acquire(io);
  fdio_fdtab[fd] = io;
  return fd;
}

int fdio_release_reserved(int fd) {
  if ((fd < 0) || (fd >= FDIO_MAX_FD)) {
    errno = EINVAL;
    return -1;
  }
  fbl::AutoLock lock(&fdio_lock);
  fdio_t* res = fdio_fdtab[fd];
  if (res != fdio_get_reserved_io()) {
    errno = EINVAL;
    return -1;
  }
  fdio_fdtab[fd] = nullptr;
  return fd;
}

// Attaches an fdio to an fdtab slot.
// The fdio must have been upref'd on behalf of the
// fdtab prior to binding.
__EXPORT
int fdio_bind_to_fd(fdio_t* io, int fd, int starting_fd) {
  fdio_t* io_to_close = nullptr;
  {
    fbl::AutoLock lock(&fdio_lock);
    if (fd < 0) {
      // If we are not given an |fd|, the |starting_fd| must be non-negative.
      if (starting_fd < 0) {
        errno = EINVAL;
        return -1;
      }

      // A negative fd implies that any free fd value can be used
      // TODO: bitmap, ffs, etc
      for (fd = starting_fd; fd < FDIO_MAX_FD; fd++) {
        if (fdio_fdtab[fd] == nullptr) {
          goto free_fd_found;
        }
      }
      errno = EMFILE;
      return -1;
    } else if (fd >= FDIO_MAX_FD) {
      errno = EINVAL;
      return -1;
    }
    io_to_close = fdio_fdtab[fd];
    if (io_to_close == io) {
      // No change, but we must remember to drop the additional reference.
      fdio_release(io_to_close);
      return fd;
    }
    if (io_to_close) {
      fdio_dupcount_release(io_to_close);
      if (fdio_get_dupcount(io_to_close) > 0) {
        // still alive in another fdtab slot
        fdio_release(io_to_close);
        io_to_close = nullptr;
      }
    }

  free_fd_found:
    fdio_dupcount_acquire(io);
    fdio_fdtab[fd] = io;
  }

  if (io_to_close) {
    fdio_get_ops(io_to_close)->close(io_to_close);
    fdio_release(io_to_close);
  }
  return fd;
}

// If a fdio_t exists for this fd and it has not been dup'd
// and is not in active use (an io operation underway, etc),
// detach it from the fdtab and return it with a single
// refcount.
__EXPORT
zx_status_t fdio_unbind_from_fd(int fd, fdio_t** out) {
  fbl::AutoLock lock(&fdio_lock);
  if ((fd < 0) || (fd >= FDIO_MAX_FD)) {
    return ZX_ERR_INVALID_ARGS;
  }
  fdio_t* io = fdio_fdtab[fd];
  if (fdio_is_reserved_or_null(io)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (fdio_get_dupcount(io) > 1) {
    return ZX_ERR_UNAVAILABLE;
  }
  if (!fdio_is_last_reference(io)) {
    return ZX_ERR_UNAVAILABLE;
  }
  fdio_dupcount_release(io);
  fdio_fdtab[fd] = nullptr;
  *out = io;
  return ZX_OK;
}

__EXPORT
fdio_t* fdio_unsafe_fd_to_io(int fd) {
  if ((fd < 0) || (fd >= FDIO_MAX_FD)) {
    return nullptr;
  }
  fdio_t* io = nullptr;
  fbl::AutoLock lock(&fdio_lock);
  io = fdio_fdtab[fd];
  if (fdio_is_reserved_or_null(io)) {
    // Never hand back the reserved io as it does not have an ops table.
    io = nullptr;
  } else {
    fdio_acquire(io);
  }
  return io;
}

zx_status_t fdio_close(fdio_t* io) { return fdio_get_ops(io)->close(io); }

// Verify the O_* flags which align with ZXIO_FS_*.
static_assert(O_PATH == ZX_FS_FLAG_VNODE_REF_ONLY, "Open Flag mismatch");
static_assert(O_ADMIN == ZX_FS_RIGHT_ADMIN, "Open Flag mismatch");
static_assert(O_CREAT == ZX_FS_FLAG_CREATE, "Open Flag mismatch");
static_assert(O_EXCL == ZX_FS_FLAG_EXCLUSIVE, "Open Flag mismatch");
static_assert(O_TRUNC == ZX_FS_FLAG_TRUNCATE, "Open Flag mismatch");
static_assert(O_DIRECTORY == ZX_FS_FLAG_DIRECTORY, "Open Flag mismatch");
static_assert(O_APPEND == ZX_FS_FLAG_APPEND, "Open Flag mismatch");
static_assert(O_NOREMOTE == ZX_FS_FLAG_NOREMOTE, "Open Flag mismatch");

// The mask of "1:1" flags which match between both open flag representations.
#define ZXIO_FS_MASK \
  (O_PATH | O_ADMIN | O_CREAT | O_EXCL | O_TRUNC | O_DIRECTORY | O_APPEND | O_NOREMOTE)

#define ZXIO_FS_FLAGS \
  (ZXIO_FS_MASK | ZX_FS_FLAG_POSIX | ZX_FS_FLAG_NOT_DIRECTORY | ZX_FS_FLAG_CLONE_SAME_RIGHTS)

// Verify that the remaining O_* flags don't overlap with the ZXIO_FS flags.
static_assert(!(O_RDONLY & ZXIO_FS_FLAGS), "Unexpected collision with ZXIO_FS_FLAGS");
static_assert(!(O_WRONLY & ZXIO_FS_FLAGS), "Unexpected collision with ZXIO_FS_FLAGS");
static_assert(!(O_RDWR & ZXIO_FS_FLAGS), "Unexpected collision with ZXIO_FS_FLAGS");
static_assert(!(O_NONBLOCK & ZXIO_FS_FLAGS), "Unexpected collision with ZXIO_FS_FLAGS");
static_assert(!(O_DSYNC & ZXIO_FS_FLAGS), "Unexpected collision with ZXIO_FS_FLAGS");
static_assert(!(O_SYNC & ZXIO_FS_FLAGS), "Unexpected collision with ZXIO_FS_FLAGS");
static_assert(!(O_RSYNC & ZXIO_FS_FLAGS), "Unexpected collision with ZXIO_FS_FLAGS");
static_assert(!(O_NOFOLLOW & ZXIO_FS_FLAGS), "Unexpected collision with ZXIO_FS_FLAGS");
static_assert(!(O_CLOEXEC & ZXIO_FS_FLAGS), "Unexpected collision with ZXIO_FS_FLAGS");
static_assert(!(O_NOCTTY & ZXIO_FS_FLAGS), "Unexpected collision with ZXIO_FS_FLAGS");
static_assert(!(O_ASYNC & ZXIO_FS_FLAGS), "Unexpected collision with ZXIO_FS_FLAGS");
static_assert(!(O_DIRECT & ZXIO_FS_FLAGS), "Unexpected collision with ZXIO_FS_FLAGS");
static_assert(!(O_LARGEFILE & ZXIO_FS_FLAGS), "Unexpected collision with ZXIO_FS_FLAGS");
static_assert(!(O_NOATIME & ZXIO_FS_FLAGS), "Unexpected collision with ZXIO_FS_FLAGS");
static_assert(!(O_TMPFILE & ZXIO_FS_FLAGS), "Unexpected collision with ZXIO_FS_FLAGS");

#define ZX_FS_FLAGS_ALLOWED_WITH_O_PATH                                          \
  (ZX_FS_FLAG_VNODE_REF_ONLY | ZX_FS_FLAG_DIRECTORY | ZX_FS_FLAG_NOT_DIRECTORY | \
   ZX_FS_FLAG_DESCRIBE)

static uint32_t fdio_flags_to_zxio(uint32_t flags) {
  uint32_t rights = 0;
  switch (flags & O_ACCMODE) {
    case O_RDONLY:
      rights |= ZX_FS_RIGHT_READABLE;
      break;
    case O_WRONLY:
      rights |= ZX_FS_RIGHT_WRITABLE;
      break;
    case O_RDWR:
      rights |= ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE;
      break;
  }

  uint32_t result = rights | ZX_FS_FLAG_DESCRIBE | (flags & ZXIO_FS_MASK);

  if (!(result & ZX_FS_FLAG_VNODE_REF_ONLY)) {
    result |= ZX_FS_FLAG_POSIX;
  }
  return result;
}

static uint32_t zxio_flags_to_fdio(uint32_t flags) {
  uint32_t result = 0;
  if ((flags & (ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE)) ==
      (ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE)) {
    result |= O_RDWR;
  } else if (flags & ZX_FS_RIGHT_WRITABLE) {
    result |= O_WRONLY;
  } else {
    result |= O_RDONLY;
  }

  result |= (flags & ZXIO_FS_MASK);
  return result;
}

// Possibly return an owned fdio_t corresponding to either the root,
// the cwd, or, for the ...at variants, dirfd. In the absolute path
// case, *path is also adjusted.
static fdio_t* fdio_iodir(const char** path, int dirfd) {
  fdio_t* iodir = nullptr;
  fbl::AutoLock lock(&fdio_lock);
  if (*path[0] == '/') {
    iodir = fdio_root_handle;
    // Since we are sending a request to the root handle, the
    // rest of the path should be canonicalized as a relative
    // path (relative to this root handle).
    while (*path[0] == '/') {
      (*path)++;
      if (*path[0] == 0) {
        *path = ".";
      }
    }
  } else if (dirfd == AT_FDCWD) {
    iodir = fdio_cwd_handle;
  } else if ((dirfd >= 0) && (dirfd < FDIO_MAX_FD)) {
    iodir = fdio_fdtab[dirfd];
  }
  if (iodir != nullptr) {
    fdio_acquire(iodir);
  }
  return iodir;
}

#define IS_SEPARATOR(c) ((c) == '/' || (c) == 0)

// Checks that if we increment this index forward, we'll
// still have enough space for a null terminator within
// PATH_MAX bytes.
#define CHECK_CAN_INCREMENT(i)         \
  if (unlikely((i) + 1 >= PATH_MAX)) { \
    return ZX_ERR_BAD_PATH;            \
  }

// Cleans an input path, transforming it to out, according to the
// rules defined by "Lexical File Names in Plan 9 or Getting Dot-Dot Right",
// accessible at: https://9p.io/sys/doc/lexnames.html
//
// Code heavily inspired by Go's filepath.Clean function, from:
// https://golang.org/src/path/filepath/path.go
//
// out is expected to be PATH_MAX bytes long.
// Sets is_dir to 'true' if the path is a directory, and 'false' otherwise.
__EXPORT
zx_status_t __fdio_cleanpath(const char* in, char* out, size_t* outlen, bool* is_dir) {
  if (in[0] == 0) {
    strcpy(out, ".");
    *outlen = 1;
    *is_dir = true;
    return ZX_OK;
  }

  bool rooted = (in[0] == '/');
  size_t in_index = 0;   // Index of the next byte to read
  size_t out_index = 0;  // Index of the next byte to write

  if (rooted) {
    out[out_index++] = '/';
    in_index++;
    *is_dir = true;
  }
  size_t dotdot = out_index;  // The output index at which '..' cannot be cleaned further.

  while (in[in_index] != 0) {
    *is_dir = true;
    if (in[in_index] == '/') {
      // 1. Reduce multiple slashes to a single slash
      CHECK_CAN_INCREMENT(in_index);
      in_index++;
    } else if (in[in_index] == '.' && IS_SEPARATOR(in[in_index + 1])) {
      // 2. Eliminate . path name elements (the current directory)
      CHECK_CAN_INCREMENT(in_index);
      in_index++;
    } else if (in[in_index] == '.' && in[in_index + 1] == '.' && IS_SEPARATOR(in[in_index + 2])) {
      CHECK_CAN_INCREMENT(in_index + 1);
      in_index += 2;
      if (out_index > dotdot) {
        // 3. Eliminate .. path elements (the parent directory) and the element that
        // precedes them.
        out_index--;
        while (out_index > dotdot && out[out_index] != '/') {
          out_index--;
        }
      } else if (rooted) {
        // 4. Eliminate .. elements that begin a rooted path, that is, replace /.. by / at
        // the beginning of a path.
        continue;
      } else if (!rooted) {
        if (out_index > 0) {
          out[out_index++] = '/';
        }
        // 5. Leave intact .. elements that begin a non-rooted path.
        out[out_index++] = '.';
        out[out_index++] = '.';
        dotdot = out_index;
      }
    } else {
      *is_dir = false;
      if ((rooted && out_index != 1) || (!rooted && out_index != 0)) {
        // Add '/' before normal path component, for non-root components.
        out[out_index++] = '/';
      }

      while (!IS_SEPARATOR(in[in_index])) {
        CHECK_CAN_INCREMENT(in_index);
        out[out_index++] = in[in_index++];
      }
    }
  }

  if (out_index == 0) {
    strcpy(out, ".");
    *outlen = 1;
    *is_dir = true;
    return ZX_OK;
  }

  // Append null character
  *outlen = out_index;
  out[out_index++] = 0;
  return ZX_OK;
}

static zx_status_t __fdio_open_at_impl(fdio_t** io, int dirfd, const char* path, int flags,
                                       uint32_t mode, bool enforce_eisdir) {
  if (path == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (path[0] == '\0') {
    return ZX_ERR_NOT_FOUND;
  }
  fdio_t* iodir = fdio_iodir(&path, dirfd);
  if (iodir == nullptr) {
    return ZX_ERR_BAD_HANDLE;
  }

  char clean[PATH_MAX];
  size_t outlen;
  bool has_ending_slash;
  zx_status_t status = __fdio_cleanpath(path, clean, &outlen, &has_ending_slash);
  if (status != ZX_OK) {
    fdio_release(iodir);
    return status;
  }
  // Emulate EISDIR behavior from
  // http://pubs.opengroup.org/onlinepubs/9699919799/functions/open.html
  bool flags_incompatible_with_directory =
      ((flags & ~O_PATH & O_ACCMODE) != O_RDONLY) || (flags & O_CREAT);
  if (enforce_eisdir && has_ending_slash && flags_incompatible_with_directory) {
    fdio_release(iodir);
    return ZX_ERR_NOT_FILE;
  }
  flags |= (has_ending_slash ? O_DIRECTORY : 0);

  uint32_t zx_flags = fdio_flags_to_zxio((uint32_t)flags);

  if (!(zx_flags & ZX_FS_FLAG_DIRECTORY)) {
    // At this point we're not sure if the path refers to a directory.
    // To emulate EISDIR behavior, if the flags are not compatible with directory,
    // use this flag to instruct open to error if the path turns out to be a directory.
    // Otherwise, opening a directory with O_RDWR will incorrectly succeed.
    if (enforce_eisdir && flags_incompatible_with_directory) {
      zx_flags |= ZX_FS_FLAG_NOT_DIRECTORY;
    }
  }
  if (zx_flags & ZX_FS_FLAG_VNODE_REF_ONLY) {
    zx_flags &= ZX_FS_FLAGS_ALLOWED_WITH_O_PATH;
  }
  status = fdio_get_ops(iodir)->open(iodir, clean, zx_flags, mode, io);
  fdio_release(iodir);
  return status;
}

static zx_status_t __fdio_open_at(fdio_t** io, int dirfd, const char* path, int flags,
                                  uint32_t mode) {
  return __fdio_open_at_impl(io, dirfd, path, flags, mode, true);
}

static zx_status_t __fdio_open_at_ignore_eisdir(fdio_t** io, int dirfd, const char* path, int flags,
                                                uint32_t mode) {
  return __fdio_open_at_impl(io, dirfd, path, flags, mode, false);
}

static zx_status_t __fdio_open(fdio_t** io, const char* path, int flags, uint32_t mode) {
  return __fdio_open_at(io, AT_FDCWD, path, flags, mode);
}

static void update_cwd_path(const char* path) {
  if (path[0] == '/') {
    // it's "absolute", but we'll still parse it as relative (from /)
    // so that we normalize the path (resolving, ., .., //, etc)
    fdio_cwd_path[0] = '/';
    fdio_cwd_path[1] = 0;
    path++;
  }

  size_t seglen;
  const char* next;
  for (; path[0]; path = next) {
    next = strchr(path, '/');
    if (next == nullptr) {
      seglen = strlen(path);
      next = path + seglen;
    } else {
      seglen = next - path;
      next++;
    }
    if (seglen == 0) {
      // empty segment, skip
      continue;
    }
    if ((seglen == 1) && (path[0] == '.')) {
      // no-change segment, skip
      continue;
    }
    if ((seglen == 2) && (path[0] == '.') && (path[1] == '.')) {
      // parent directory, remove the trailing path segment from cwd_path
      char* x = strrchr(fdio_cwd_path, '/');
      if (x == nullptr) {
        // shouldn't ever happen
        goto wat;
      }
      // remove the current trailing path segment from cwd
      if (x == fdio_cwd_path) {
        // but never remove the first /
        fdio_cwd_path[1] = 0;
      } else {
        x[0] = 0;
      }
      continue;
    }
    // regular path segment, append to cwd_path
    size_t len = strlen(fdio_cwd_path);
    if ((len + seglen + 2) >= PATH_MAX) {
      // doesn't fit, shouldn't happen, but...
      goto wat;
    }
    if (len != 1) {
      // if len is 1, path is "/", so don't append a '/'
      fdio_cwd_path[len++] = '/';
    }
    memcpy(fdio_cwd_path + len, path, seglen);
    fdio_cwd_path[len + seglen] = 0;
  }
  return;

wat:
  strcpy(fdio_cwd_path, "(unknown)");
}

// Opens the directory containing path
//
// Returns the non-directory portion of the path in 'out', which
// must be a buffer that can fit [NAME_MAX + 1] characters.
static zx_status_t __fdio_opendir_containing_at(fdio_t** io, int dirfd, const char* path,
                                                char* out) {
  if (path == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  fdio_t* iodir = fdio_iodir(&path, dirfd);
  if (iodir == nullptr) {
    return ZX_ERR_BAD_HANDLE;
  }

  char clean[PATH_MAX];
  size_t pathlen;
  bool is_dir;
  zx_status_t status = __fdio_cleanpath(path, clean, &pathlen, &is_dir);
  if (status != ZX_OK) {
    fdio_release(iodir);
    return status;
  }

  // Find the last '/'; copy everything after it.
  size_t i = 0;
  for (i = pathlen - 1; i > 0; i--) {
    if (clean[i] == '/') {
      clean[i] = 0;
      i++;
      break;
    }
  }

  // clean[i] is now the start of the name
  size_t namelen = pathlen - i;
  if (namelen + (is_dir ? 1 : 0) > NAME_MAX) {
    fdio_release(iodir);
    return ZX_ERR_BAD_PATH;
  }

  // Copy the trailing 'name' to out.
  memcpy(out, clean + i, namelen);
  if (is_dir) {
    // TODO(fxbug.dev/37408): Propagate whether path is directory without using
    // trailing backslash to simplify server-side path parsing.
    // This might require refactoring trailing backslash checks out of
    // lower filesystem layers and associated FIDL APIs.

    out[namelen++] = '/';
  }
  out[namelen] = 0;

  if (i == 0 && clean[i] != '/') {
    clean[0] = '.';
    clean[1] = 0;
  }

  zx_status_t r =
      fdio_get_ops(iodir)->open(iodir, clean, fdio_flags_to_zxio(O_RDONLY | O_DIRECTORY), 0, io);
  fdio_release(iodir);
  return r;
}

// hook into libc process startup
// this is called prior to main to set up the fdio world
// and thus does not use the fdio_lock
//
// extern "C" is required here, since the corresponding declaration is in an internal musl header:
// zircon/third_party/ulib/musl/src/internal/libc.h
extern "C" __EXPORT void __libc_extensions_init(uint32_t handle_count, zx_handle_t handle[],
                                                uint32_t handle_info[], uint32_t name_count,
                                                char** names) {
  zx_status_t status = fdio_ns_create(&fdio_root_ns);
  ZX_ASSERT_MSG(status == ZX_OK, "Failed to create root namespace");

  int stdio_fd = -1;

  // extract handles we care about
  for (uint32_t n = 0; n < handle_count; n++) {
    unsigned arg = PA_HND_ARG(handle_info[n]);
    zx_handle_t h = handle[n];

    // precalculate the fd from |arg|, for FDIO cases to use.
    unsigned arg_fd = arg & (~FDIO_FLAG_USE_FOR_STDIO);

    switch (PA_HND_TYPE(handle_info[n])) {
      case PA_FD: {
        fdio_t* io = nullptr;
        status = fdio_create(h, &io);
        if (status != ZX_OK) {
          zx_handle_close(h);
          continue;
        }
        ZX_ASSERT_MSG(arg_fd < FDIO_MAX_FD,
                      "unreasonably large fd number %u in PA_FD (must be less than %u)", arg_fd,
                      FDIO_MAX_FD);
        ZX_ASSERT_MSG(fdio_fdtab[arg_fd] == nullptr, "duplicate fd number %u in PA_FD", arg_fd);
        fdio_fdtab[arg_fd] = io;
        fdio_dupcount_acquire(fdio_fdtab[arg_fd]);
        break;
      }
      case PA_NS_DIR:
        // we always continue here to not steal the
        // handles from higher level code that may
        // also need access to the namespace
        if (arg >= name_count) {
          continue;
        }
        fdio_ns_bind(fdio_root_ns, names[arg], h);
        continue;
      default:
        // unknown handle, leave it alone
        continue;
    }
    handle[n] = 0;
    handle_info[n] = 0;

    // If we reach here then the handle is a PA_FD type (a file descriptor),
    // so check for a bit flag indicating that it should be duped
    // into 0/1/2 to become all of stdin/out/err
    if ((arg & FDIO_FLAG_USE_FOR_STDIO) && (arg_fd < FDIO_MAX_FD)) {
      stdio_fd = arg_fd;
    }
  }

  const char* cwd = getenv("PWD");
  cwd = (cwd == nullptr) ? "/" : cwd;

  update_cwd_path(cwd);

  fdio_t* use_for_stdio = (stdio_fd >= 0) ? fdio_fdtab[stdio_fd] : nullptr;

  // configure stdin/out/err if not init'd
  for (uint32_t n = 0; n < 3; n++) {
    if (fdio_fdtab[n] == nullptr) {
      if (use_for_stdio) {
        fdio_acquire(use_for_stdio);
        fdio_fdtab[n] = use_for_stdio;
      } else {
        fdio_fdtab[n] = fdio_null_create();
      }
      fdio_dupcount_acquire(fdio_fdtab[n]);
    }
  }

  ZX_ASSERT(!fdio_root_handle);
  fdio_root_handle = fdio_ns_open_root(fdio_root_ns);

  if (fdio_root_handle) {
    __fdio_open(&fdio_cwd_handle, fdio_cwd_path, O_RDONLY | O_DIRECTORY, 0);
  } else {
    // placeholder null handle
    fdio_root_handle = fdio_null_create();
  }
  if (fdio_cwd_handle == nullptr) {
    fdio_cwd_handle = fdio_null_create();
  }
}

// Clean up during process teardown. This runs after atexit hooks in
// libc. It continues to hold the fdio lock until process exit, to
// prevent other threads from racing on file descriptors.
//
// extern "C" is required here, since the corresponding declaration is in an internal musl header:
// zircon/third_party/ulib/musl/src/internal/libc.h
extern "C" __EXPORT void __libc_extensions_fini(void) __TA_ACQUIRE(&fdio_lock) {
  mtx_lock(&fdio_lock);
  for (int fd = 0; fd < FDIO_MAX_FD; fd++) {
    fdio_t* io = fdio_fdtab[fd];
    if (!fdio_is_reserved_or_null(io)) {
      fdio_fdtab[fd] = nullptr;
      fdio_dupcount_release(io);
      if (fdio_get_dupcount(io) == 0) {
        fdio_get_ops(io)->close(io);
        fdio_release(io);
      }
    }
  }
}

__EXPORT
zx_status_t fdio_ns_get_installed(fdio_ns_t** ns) {
  zx_status_t status = ZX_OK;
  fbl::AutoLock lock(&fdio_lock);
  if (fdio_root_ns == nullptr) {
    status = ZX_ERR_NOT_FOUND;
  } else {
    *ns = fdio_root_ns;
  }
  return status;
}

zx_status_t fdio_wait(fdio_t* io, uint32_t events, zx::time deadline, uint32_t* out_pending) {
  zx_handle_t h = ZX_HANDLE_INVALID;
  zx_signals_t signals = 0;
  fdio_get_ops(io)->wait_begin(io, events, &h, &signals);
  if (h == ZX_HANDLE_INVALID)
    // Wait operation is not applicable to the handle.
    return ZX_ERR_INVALID_ARGS;

  zx_signals_t pending;
  zx_status_t status = zx_object_wait_one(h, signals, deadline.get(), &pending);
  if (status == ZX_OK || status == ZX_ERR_TIMED_OUT) {
    fdio_get_ops(io)->wait_end(io, pending, &events);
    if (out_pending != nullptr) {
      *out_pending = events;
    }
  }

  return status;
}

__EXPORT
zx_status_t fdio_wait_fd(int fd, uint32_t events, uint32_t* out_pending, zx_time_t deadline) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr)
    return ZX_ERR_BAD_HANDLE;

  zx_status_t status = fdio_wait(io, events, zx::time(deadline), out_pending);

  fdio_release(io);
  return status;
}

static zx_status_t fdio_stat(fdio_t* io, struct stat* s) {
  zxio_node_attributes_t attr;
  zx_status_t status = fdio_get_ops(io)->get_attr(io, &attr);
  if (status != ZX_OK) {
    return status;
  }

  memset(s, 0, sizeof(struct stat));
  s->st_mode = fdio_get_ops(io)->convert_to_posix_mode(io, attr.protocols, attr.abilities);
  s->st_ino = attr.has.id ? attr.id : fio::INO_UNKNOWN;
  s->st_size = attr.content_size;
  s->st_blksize = VNATTR_BLKSIZE;
  s->st_blocks = attr.storage_size / VNATTR_BLKSIZE;
  s->st_nlink = attr.link_count;
  s->st_ctim.tv_sec = attr.creation_time / ZX_SEC(1);
  s->st_ctim.tv_nsec = attr.creation_time % ZX_SEC(1);
  s->st_mtim.tv_sec = attr.modification_time / ZX_SEC(1);
  s->st_mtim.tv_nsec = attr.modification_time % ZX_SEC(1);
  return ZX_OK;
}

// TODO(ZX-974): determine complete correct mapping
int fdio_status_to_errno(zx_status_t status) {
  switch (status) {
    case ZX_ERR_NOT_FOUND:
      return ENOENT;
    case ZX_ERR_NO_MEMORY:
      return ENOMEM;
    case ZX_ERR_INVALID_ARGS:
      return EINVAL;
    case ZX_ERR_BUFFER_TOO_SMALL:
      return EINVAL;
    case ZX_ERR_TIMED_OUT:
      return ETIMEDOUT;
    case ZX_ERR_UNAVAILABLE:
      return EBUSY;
    case ZX_ERR_ALREADY_EXISTS:
      return EEXIST;
    case ZX_ERR_PEER_CLOSED:
      return EPIPE;
    case ZX_ERR_BAD_STATE:
      return EPIPE;
    case ZX_ERR_BAD_PATH:
      return ENAMETOOLONG;
    case ZX_ERR_IO:
      return EIO;
    case ZX_ERR_NOT_FILE:
      return EISDIR;
    case ZX_ERR_NOT_DIR:
      return ENOTDIR;
    case ZX_ERR_NOT_SUPPORTED:
      return ENOTSUP;
    case ZX_ERR_WRONG_TYPE:
      return ENOTSUP;
    case ZX_ERR_OUT_OF_RANGE:
      return EINVAL;
    case ZX_ERR_NO_RESOURCES:
      return ENOMEM;
    case ZX_ERR_BAD_HANDLE:
      return EBADF;
    case ZX_ERR_ACCESS_DENIED:
      return EACCES;
    case ZX_ERR_SHOULD_WAIT:
      return EAGAIN;
    case ZX_ERR_FILE_BIG:
      return EFBIG;
    case ZX_ERR_NO_SPACE:
      return ENOSPC;
    case ZX_ERR_NOT_EMPTY:
      return ENOTEMPTY;
    case ZX_ERR_IO_REFUSED:
      return ECONNREFUSED;
    case ZX_ERR_IO_INVALID:
      return EIO;
    case ZX_ERR_CANCELED:
      return EBADF;
    case ZX_ERR_PROTOCOL_NOT_SUPPORTED:
      return EPROTONOSUPPORT;
    case ZX_ERR_ADDRESS_UNREACHABLE:
      return ENETUNREACH;
    case ZX_ERR_ADDRESS_IN_USE:
      return EADDRINUSE;
    case ZX_ERR_NOT_CONNECTED:
      return ENOTCONN;
    case ZX_ERR_CONNECTION_REFUSED:
      return ECONNREFUSED;
    case ZX_ERR_CONNECTION_RESET:
      return ECONNRESET;
    case ZX_ERR_CONNECTION_ABORTED:
      return ECONNABORTED;

    // No specific translation, so return a generic value.
    default:
      return EIO;
  }
}

// The functions from here on provide implementations of fd and path
// centric posix-y io operations.

// extern "C" is required here, since the corresponding declaration is in an internal musl header:
// zircon/third_party/ulib/musl/src/internal/stdio_impl.h
extern "C" __EXPORT zx_status_t _mmap_file(size_t offset, size_t len, zx_vm_option_t zx_options,
                                           int flags, int fd, off_t fd_off, uintptr_t* out) {
  fdio_t* io;
  if ((io = fd_to_io(fd)) == nullptr) {
    return ZX_ERR_BAD_HANDLE;
  }

  int vflags = zx_options | (flags & MAP_PRIVATE ? fio::VMO_FLAG_PRIVATE : 0);

  zx::vmo vmo;
  size_t size;
  zx_status_t r = zxio_vmo_get(fdio_get_zxio(io), vflags, vmo.reset_and_get_address(), &size);
  fdio_release(io);
  // On POSIX, performing mmap on an fd which does not support it returns an access denied error.
  if (r == ZX_ERR_NOT_SUPPORTED) {
    return ZX_ERR_ACCESS_DENIED;
  }
  if (r < 0) {
    return r;
  }

  uintptr_t ptr = 0;
  zx_options |= ZX_VM_ALLOW_FAULTS;
  r = zx_vmar_map(zx_vmar_root_self(), zx_options, offset, vmo.get(), fd_off, len, &ptr);
  // TODO: map this as shared if we ever implement forking
  if (r < 0) {
    return r;
  }

  *out = ptr;
  return ZX_OK;
}

__EXPORT
int unlinkat(int dirfd, const char* path, int flags) {
  char name[NAME_MAX + 1];
  fdio_t* io;
  zx_status_t r;
  if ((r = __fdio_opendir_containing_at(&io, dirfd, path, name)) < 0) {
    return ERROR(r);
  }
  r = fdio_get_ops(io)->unlink(io, name, strlen(name));
  fdio_get_ops(io)->close(io);
  fdio_release(io);
  return STATUS(r);
}

__EXPORT
ssize_t readv(int fd, const struct iovec* iov, int iovcnt) {
  struct msghdr msg = {};
  msg.msg_iov = const_cast<struct iovec*>(iov);
  msg.msg_iovlen = iovcnt;
  return recvmsg(fd, &msg, 0);
}

__EXPORT
ssize_t writev(int fd, const struct iovec* iov, int iovcnt) {
  struct msghdr msg = {};
  msg.msg_iov = const_cast<struct iovec*>(iov);
  msg.msg_iovlen = iovcnt;
  return sendmsg(fd, &msg, 0);
}

__EXPORT
ssize_t preadv(int fd, const struct iovec* iov, int iovcnt, off_t offset) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  bool nonblocking = *fdio_get_ioflag(io) & IOFLAG_NONBLOCK;
  zx::time deadline = zx::deadline_after(*fdio_get_rcvtimeo(io));

  zx_iovec_t zx_iov[iovcnt];
  for (int i = 0; i < iovcnt; ++i) {
    zx_iov[i] = {
        .buffer = iov[i].iov_base,
        .capacity = iov[i].iov_len,
    };
  }

  for (;;) {
    size_t actual;
    zx_status_t status = zxio_readv_at(fdio_get_zxio(io), offset, zx_iov, iovcnt, 0, &actual);
    if (status == ZX_ERR_SHOULD_WAIT && !nonblocking) {
      if (fdio_wait(io, FDIO_EVT_READABLE, deadline, nullptr) != ZX_ERR_TIMED_OUT) {
        continue;
      }
    }
    fdio_release(io);
    if (status != ZX_OK) {
      return ERROR(status);
    }
    return actual;
  }
}

__EXPORT
ssize_t pwritev(int fd, const struct iovec* iov, int iovcnt, off_t offset) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  bool nonblocking = *fdio_get_ioflag(io) & IOFLAG_NONBLOCK;
  zx::time deadline = zx::deadline_after(*fdio_get_sndtimeo(io));

  zx_iovec_t zx_iov[iovcnt];
  for (int i = 0; i < iovcnt; ++i) {
    zx_iov[i] = {
        .buffer = iov[i].iov_base,
        .capacity = iov[i].iov_len,
    };
  }

  for (;;) {
    size_t actual;
    zx_status_t status = zxio_writev_at(fdio_get_zxio(io), offset, zx_iov, iovcnt, 0, &actual);
    if (status == ZX_ERR_SHOULD_WAIT && !nonblocking) {
      if (fdio_wait(io, FDIO_EVT_WRITABLE, deadline, nullptr) != ZX_ERR_TIMED_OUT) {
        continue;
      }
    }
    fdio_release(io);
    if (status != ZX_OK) {
      return ERROR(status);
    }
    return actual;
  }
}

__EXPORT
ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
  struct iovec iov = {};
  iov.iov_base = buf;
  iov.iov_len = count;
  return preadv(fd, &iov, 1, offset);
}

__EXPORT
ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset) {
  struct iovec iov = {};
  iov.iov_base = const_cast<void*>(buf);
  iov.iov_len = count;
  return pwritev(fd, &iov, 1, offset);
}

__EXPORT
ssize_t read(int fd, void* buf, size_t count) {
  struct iovec iov = {};
  iov.iov_base = buf;
  iov.iov_len = count;
  return readv(fd, &iov, 1);
}

__EXPORT
ssize_t write(int fd, const void* buf, size_t count) {
  struct iovec iov = {};
  iov.iov_base = const_cast<void*>(buf);
  iov.iov_len = count;
  return writev(fd, &iov, 1);
}

__EXPORT
int close(int fd) {
  mtx_lock(&fdio_lock);
  if ((fd < 0) || (fd >= FDIO_MAX_FD) || (fdio_fdtab[fd] == nullptr)) {
    mtx_unlock(&fdio_lock);
    return ERRNO(EBADF);
  }
  fdio_t* io = fdio_fdtab[fd];
  fdio_dupcount_release(io);
  fdio_fdtab[fd] = nullptr;
  if (fdio_get_dupcount(io) > 0) {
    // still alive in other fdtab slots
    mtx_unlock(&fdio_lock);
    fdio_release(io);
    return ZX_OK;
  } else {
    mtx_unlock(&fdio_lock);
    int r = fdio_get_ops(io)->close(io);
    fdio_release(io);
    return STATUS(r);
  }
}

static int fdio_dup(int oldfd, int newfd, int starting_fd) {
  fdio_t* io = fd_to_io(oldfd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  int fd = fdio_bind_to_fd(io, newfd, starting_fd);
  if (fd < 0) {
    fdio_release(io);
  }
  return fd;
}

__EXPORT
int dup2(int oldfd, int newfd) { return fdio_dup(oldfd, newfd, 0); }

__EXPORT
int dup(int oldfd) { return fdio_dup(oldfd, -1, 0); }

__EXPORT
int dup3(int oldfd, int newfd, int flags) {
  // dup3 differs from dup2 in that it fails with EINVAL, rather
  // than being a no op, on being given the same fd for both old and
  // new.
  if (oldfd == newfd) {
    return ERRNO(EINVAL);
  }

  if (flags != 0 && flags != O_CLOEXEC) {
    return ERRNO(EINVAL);
  }

  // TODO(ZX-973) Implement O_CLOEXEC.
  return fdio_dup(oldfd, newfd, 0);
}

__EXPORT
int fcntl(int fd, int cmd, ...) {
// Note that it is not safe to pull out the int out of the
// variadic arguments at the top level, as callers are not
// required to pass anything for many of the commands.
#define GET_INT_ARG(ARG)       \
  va_list args;                \
  va_start(args, cmd);         \
  int ARG = va_arg(args, int); \
  va_end(args)

  switch (cmd) {
    case F_DUPFD:
    case F_DUPFD_CLOEXEC: {
      // TODO(ZX-973) Implement CLOEXEC.
      GET_INT_ARG(starting_fd);
      return fdio_dup(fd, -1, starting_fd);
    }
    case F_GETFD: {
      fdio_t* io = fd_to_io(fd);
      if (io == nullptr) {
        return ERRNO(EBADF);
      }
      int flags = (int)(*fdio_get_ioflag(io) & IOFLAG_FD_FLAGS);
      // POSIX mandates that the return value be nonnegative if successful.
      assert(flags >= 0);
      fdio_release(io);
      return flags;
    }
    case F_SETFD: {
      fdio_t* io = fd_to_io(fd);
      if (io == nullptr) {
        return ERRNO(EBADF);
      }
      GET_INT_ARG(flags);
      // TODO(ZX-973) Implement CLOEXEC.
      *fdio_get_ioflag(io) &= ~IOFLAG_FD_FLAGS;
      *fdio_get_ioflag(io) |= (uint32_t)flags & IOFLAG_FD_FLAGS;
      fdio_release(io);
      return 0;
    }
    case F_GETFL: {
      fdio_t* io = fd_to_io(fd);
      if (io == nullptr) {
        return ERRNO(EBADF);
      }
      uint32_t flags = 0;
      zx_status_t r = fdio_get_ops(io)->get_flags(io, &flags);
      if (r == ZX_ERR_NOT_SUPPORTED) {
        // We treat this as non-fatal, as it's valid for a remote to
        // simply not support FCNTL, but we still want to correctly
        // report the state of the (local) NONBLOCK flag
        flags = 0;
        r = ZX_OK;
      }
      flags = zxio_flags_to_fdio(flags);
      if (*fdio_get_ioflag(io) & IOFLAG_NONBLOCK) {
        flags |= O_NONBLOCK;
      }
      fdio_release(io);
      if (r < 0) {
        return STATUS(r);
      }
      return flags;
    }
    case F_SETFL: {
      fdio_t* io = fd_to_io(fd);
      if (io == nullptr) {
        return ERRNO(EBADF);
      }
      GET_INT_ARG(n);

      zx_status_t r;
      uint32_t flags = fdio_flags_to_zxio(n & ~O_NONBLOCK);
      r = fdio_get_ops(io)->set_flags(io, flags);

      // Some remotes don't support setting flags; we
      // can adjust their local flags anyway if NONBLOCK
      // is the only bit being toggled.
      if (r == ZX_ERR_NOT_SUPPORTED && ((n | O_NONBLOCK) == O_NONBLOCK)) {
        r = ZX_OK;
      }

      if (r != ZX_OK) {
        n = STATUS(r);
      } else {
        if (n & O_NONBLOCK) {
          *fdio_get_ioflag(io) |= IOFLAG_NONBLOCK;
        } else {
          *fdio_get_ioflag(io) &= ~IOFLAG_NONBLOCK;
        }
        n = 0;
      }
      fdio_release(io);
      return n;
    }
    case F_GETOWN:
    case F_SETOWN:
      // TODO(kulakowski) Socket support.
      return ERRNO(ENOSYS);
    case F_GETLK:
    case F_SETLK:
    case F_SETLKW:
      // TODO(kulakowski) Advisory file locking support.
      return ERRNO(ENOSYS);
    default:
      return ERRNO(EINVAL);
  }

#undef GET_INT_ARG
}

__EXPORT
off_t lseek(int fd, off_t offset, int whence) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }

  static_assert(SEEK_SET == ZXIO_SEEK_ORIGIN_START, "");
  static_assert(SEEK_CUR == ZXIO_SEEK_ORIGIN_CURRENT, "");
  static_assert(SEEK_END == ZXIO_SEEK_ORIGIN_END, "");

  size_t result = 0u;
  zx_status_t status = zxio_seek(fdio_get_zxio(io), whence, offset, &result);
  fdio_release(io);
  if (status == ZX_ERR_WRONG_TYPE) {
    // Although 'ESPIPE' is a bit of a misnomer, it is the valid errno
    // for any fd which does not implement seeking (i.e., for pipes,
    // sockets, etc).
    return ERRNO(ESPIPE);
  }
  return status != ZX_OK ? STATUS(status) : (off_t)result;
}

static int truncateat(int dirfd, const char* path, off_t len) {
  fdio_t* io;
  zx_status_t r;

  if ((r = __fdio_open_at(&io, dirfd, path, O_WRONLY, 0)) < 0) {
    return ERROR(r);
  }
  r = fdio_get_ops(io)->truncate(io, len);
  fdio_close(io);
  fdio_release(io);
  return STATUS(r);
}

__EXPORT
int truncate(const char* path, off_t len) { return truncateat(AT_FDCWD, path, len); }

__EXPORT
int ftruncate(int fd, off_t len) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }

  zx_status_t r = fdio_get_ops(io)->truncate(io, len);
  fdio_release(io);
  return STATUS(r);
}

// Filesystem operations (such as rename and link) which act on multiple paths
// have some additional complexity on Zircon. These operations (eventually) act
// on two pairs of variables: a source parent vnode + name, and a target parent
// vnode + name. However, the loose coupling of these pairs can make their
// correspondence difficult, especially when accessing each parent vnode may
// involve crossing various filesystem boundaries.
//
// To resolve this problem, these kinds of operations involve:
// - Opening the source parent vnode directly.
// - Opening the target parent vnode directly, + acquiring a "vnode token".
// - Sending the real operation + names to the source parent vnode, along with
//   the "vnode token" representing the target parent vnode.
//
// Using zircon kernel primitives (cookies) to authenticate the vnode token, this
// allows these multi-path operations to mix absolute / relative paths and cross
// mount points with ease.
static int two_path_op_at(int olddirfd, const char* oldpath, int newdirfd, const char* newpath,
                          two_path_op fdio_ops::*op_getter) {
  char oldname[NAME_MAX + 1];
  fdio_t* io_oldparent;
  zx_status_t status = ZX_OK;
  if ((status = __fdio_opendir_containing_at(&io_oldparent, olddirfd, oldpath, oldname)) < 0) {
    return ERROR(status);
  }

  char newname[NAME_MAX + 1];
  fdio_t* io_newparent;
  if ((status = __fdio_opendir_containing_at(&io_newparent, newdirfd, newpath, newname)) < 0) {
    goto oldparent_open;
  }

  zx_handle_t token;
  status = fdio_get_ops(io_newparent)->get_token(io_newparent, &token);
  if (status < 0) {
    goto newparent_open;
  }
  status = (fdio_get_ops(io_oldparent)->*op_getter)(io_oldparent, oldname, strlen(oldname), token,
                                                    newname, strlen(newname));

newparent_open:
  fdio_get_ops(io_newparent)->close(io_newparent);
  fdio_release(io_newparent);
oldparent_open:
  fdio_get_ops(io_oldparent)->close(io_oldparent);
  fdio_release(io_oldparent);
  return STATUS(status);
}

__EXPORT
int renameat(int olddirfd, const char* oldpath, int newdirfd, const char* newpath) {
  return two_path_op_at(olddirfd, oldpath, newdirfd, newpath, &fdio_ops::rename);
}

__EXPORT
int rename(const char* oldpath, const char* newpath) {
  return two_path_op_at(AT_FDCWD, oldpath, AT_FDCWD, newpath, &fdio_ops::rename);
}

__EXPORT
int link(const char* oldpath, const char* newpath) {
  return two_path_op_at(AT_FDCWD, oldpath, AT_FDCWD, newpath, &fdio_ops::link);
}

__EXPORT
int unlink(const char* path) { return unlinkat(AT_FDCWD, path, 0); }

static int vopenat(int dirfd, const char* path, int flags, va_list args) {
  fdio_t* io = nullptr;
  zx_status_t r;
  int fd;
  uint32_t mode = 0;

  if (flags & O_CREAT) {
    if (flags & O_DIRECTORY) {
      // The behavior of open with O_CREAT | O_DIRECTORY is underspecified
      // in POSIX. To help avoid programmer error, we explicitly disallow
      // the combination.
      return ERRNO(EINVAL);
    }
    mode = va_arg(args, uint32_t) & 0777;
  }
  if ((r = __fdio_open_at(&io, dirfd, path, flags, mode)) != ZX_OK) {
    return ERROR(r);
  }
  if (flags & O_NONBLOCK) {
    *fdio_get_ioflag(io) |= IOFLAG_NONBLOCK;
  }
  if ((fd = fdio_bind_to_fd(io, -1, 0)) < 0) {
    fdio_get_ops(io)->close(io);
    fdio_release(io);
    return ERRNO(EMFILE);
  }
  return fd;
}

__EXPORT
int open(const char* path, int flags, ...) {
  va_list ap;
  va_start(ap, flags);
  int ret = vopenat(AT_FDCWD, path, flags, ap);
  va_end(ap);
  return ret;
}

__EXPORT
int openat(int dirfd, const char* path, int flags, ...) {
  va_list ap;
  va_start(ap, flags);
  int ret = vopenat(dirfd, path, flags, ap);
  va_end(ap);
  return ret;
}

__EXPORT
int mkdir(const char* path, mode_t mode) { return mkdirat(AT_FDCWD, path, mode); }

__EXPORT
int mkdirat(int dirfd, const char* path, mode_t mode) {
  fdio_t* io = nullptr;
  zx_status_t r;

  mode = (mode & 0777) | S_IFDIR;

  if ((r = __fdio_open_at_ignore_eisdir(&io, dirfd, path, O_RDONLY | O_CREAT | O_EXCL, mode)) < 0) {
    return ERROR(r);
  }
  fdio_get_ops(io)->close(io);
  fdio_release(io);
  return 0;
}

__EXPORT
int fsync(int fd) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  zx_status_t status = zxio_sync(fdio_get_zxio(io));
  fdio_release(io);
  return STATUS(status);
}

__EXPORT
int fdatasync(int fd) {
  // TODO(smklein): fdatasync does not need to flush metadata under certain
  // circumstances -- however, for now, this implementation will appear
  // functionally the same (if a little slower).
  return fsync(fd);
}

__EXPORT
int syncfs(int fd) {
  // TODO(smklein): Currently, fsync syncs the entire filesystem, not just
  // the target file descriptor. These functions should use different sync
  // mechanisms, where fsync is more fine-grained.
  return fsync(fd);
}

__EXPORT
int fstat(int fd, struct stat* s) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  int r = STATUS(fdio_stat(io, s));
  fdio_release(io);
  return r;
}

__EXPORT
int fstatat(int dirfd, const char* fn, struct stat* s, int flags) {
  fdio_t* io;
  zx_status_t r;

  if ((r = __fdio_open_at(&io, dirfd, fn, O_PATH, 0)) < 0) {
    return ERROR(r);
  }
  r = fdio_stat(io, s);
  fdio_close(io);
  fdio_release(io);
  return STATUS(r);
}

__EXPORT
int stat(const char* fn, struct stat* s) { return fstatat(AT_FDCWD, fn, s, 0); }

__EXPORT
int lstat(const char* path, struct stat* buf) { return stat(path, buf); }

__EXPORT
char* realpath(const char* __restrict filename, char* __restrict resolved) {
  ssize_t r;
  struct stat st;
  char tmp[PATH_MAX];
  size_t outlen;
  bool is_dir;

  if (!filename) {
    errno = EINVAL;
    return nullptr;
  }

  if (filename[0] != '/') {
    // Convert 'filename' from a relative path to an absolute path.
    size_t file_len = strlen(filename);
    char tmp2[PATH_MAX];
    size_t cwd_len = 0;
    {
      fbl::AutoLock cwd_lock(&fdio_cwd_lock);
      cwd_len = strlen(fdio_cwd_path);
      if (cwd_len + 1 + file_len >= PATH_MAX) {
        errno = ENAMETOOLONG;
        return nullptr;
      }
      memcpy(tmp2, fdio_cwd_path, cwd_len);
    }
    tmp2[cwd_len] = '/';
    strcpy(tmp2 + cwd_len + 1, filename);
    zx_status_t status = __fdio_cleanpath(tmp2, tmp, &outlen, &is_dir);
    if (status != ZX_OK) {
      errno = EINVAL;
      return nullptr;
    }
  } else {
    // Clean the provided absolute path
    zx_status_t status = __fdio_cleanpath(filename, tmp, &outlen, &is_dir);
    if (status != ZX_OK) {
      errno = EINVAL;
      return nullptr;
    }

    r = stat(tmp, &st);
    if (r < 0) {
      return nullptr;
    }
  }
  return resolved ? strcpy(resolved, tmp) : strdup(tmp);
}

static zx_status_t zx_utimens(fdio_t* io, const std::timespec times[2], int flags) {
  zxio_node_attributes_t attr = {};

  zx_time_t modification_time;
  // Extract modify time.
  if (times == nullptr || times[1].tv_nsec == UTIME_NOW) {
    std::timespec ts;
    if (!std::timespec_get(&ts, TIME_UTC)) {
      return ZX_ERR_UNAVAILABLE;
    }
    modification_time = zx_time_from_timespec(ts);
  } else {
    modification_time = zx_time_from_timespec(times[1]);
  }

  if (times == nullptr || times[1].tv_nsec != UTIME_OMIT) {
    // For setattr, tell which fields are valid.
    ZXIO_NODE_ATTR_SET(attr, modification_time, modification_time);
  }

  // set time(s) on underlying object
  return fdio_get_ops(io)->set_attr(io, &attr);
}

__EXPORT
int utimensat(int dirfd, const char* path, const struct timespec times[2], int flags) {
  fdio_t* io;
  zx_status_t r;

  // TODO(orr): AT_SYMLINK_NOFOLLOW
  if ((flags & AT_SYMLINK_NOFOLLOW) != 0) {
    // Allow this flag - don't return an error.  Fuchsia does not support
    // symlinks, so don't break utilities (like tar) that use this flag.
  }

  if ((r = __fdio_open_at_ignore_eisdir(&io, dirfd, path, O_WRONLY, 0)) < 0) {
    return ERROR(r);
  }
  r = zx_utimens(io, times, 0);
  fdio_close(io);
  fdio_release(io);
  return STATUS(r);
}

__EXPORT
int futimens(int fd, const struct timespec times[2]) {
  fdio_t* io = fd_to_io(fd);
  zx_status_t r = zx_utimens(io, times, 0);
  fdio_release(io);
  return STATUS(r);
}

static int socketpair_create(int fd[2], uint32_t options) {
  fdio_t *a, *b;
  int r = fdio_pipe_pair(&a, &b, options);
  if (r < 0) {
    return ERROR(r);
  }
  fd[0] = fdio_bind_to_fd(a, -1, 0);
  if (fd[0] < 0) {
    int errno_ = errno;
    fdio_close(a);
    fdio_release(a);
    fdio_close(b);
    fdio_release(b);
    return ERRNO(errno_);
  }
  fd[1] = fdio_bind_to_fd(b, -1, 0);
  if (fd[1] < 0) {
    int errno_ = errno;
    close(fd[0]);
    fdio_close(b);
    fdio_release(b);
    return ERRNO(errno_);
  }
  return 0;
}

__EXPORT
int pipe2(int pipefd[2], int flags) {
  const int allowed_flags = O_NONBLOCK | O_CLOEXEC;
  if (flags & ~allowed_flags) {
    return ERRNO(EINVAL);
  }

  return socketpair_create(pipefd, 0);
}

__EXPORT
int pipe(int pipefd[2]) { return pipe2(pipefd, 0); }

__EXPORT
int socketpair(int domain, int type, int protocol, int fd[2]) {
  uint32_t options = 0;
  switch (type) {
    case SOCK_DGRAM:
      options = ZX_SOCKET_DATAGRAM;
      break;
    case SOCK_STREAM:
      options = ZX_SOCKET_STREAM;
      break;
    default:
      errno = EPROTOTYPE;
      return -1;
  }

  if (domain != AF_UNIX) {
    errno = EAFNOSUPPORT;
    return -1;
  }
  if (protocol != 0) {
    errno = EPROTONOSUPPORT;
    return -1;
  }

  return socketpair_create(fd, options);
}

__EXPORT
int faccessat(int dirfd, const char* filename, int amode, int flag) {
  // First, check that the flags and amode are valid.
  const int allowed_flags = AT_EACCESS;
  if (flag & (~allowed_flags)) {
    return ERRNO(EINVAL);
  }

  // amode is allowed to be either a subset of this mask, or just F_OK.
  const int allowed_modes = R_OK | W_OK | X_OK;
  if (amode != F_OK && (amode & (~allowed_modes))) {
    return ERRNO(EINVAL);
  }

  fdio_t* io;
  zx_status_t status;
  if (amode == F_OK) {
    // Check that the file exists a la fstatat.
    if ((status = __fdio_open_at(&io, dirfd, filename, O_PATH, 0)) < 0) {
      return ERROR(status);
    }
    struct stat s;
    status = fdio_stat(io, &s);
  } else {
    // Check that the file has each of the permissions in mode.
    // Ignore X_OK, since it does not apply to our permission model
    amode &= ~X_OK;
    uint32_t rights_flags = 0;
    switch (amode & (R_OK | W_OK)) {
      case R_OK:
        rights_flags = O_RDONLY;
        break;
      case W_OK:
        rights_flags = O_WRONLY;
        break;
      case R_OK | W_OK:
        rights_flags = O_RDWR;
        break;
    }
    if ((status = __fdio_open_at_ignore_eisdir(&io, dirfd, filename, rights_flags, 0)) < 0) {
      return ERROR(status);
    }
  }
  fdio_close(io);
  fdio_release(io);
  return STATUS(status);
}

__EXPORT
char* getcwd(char* buf, size_t size) {
  char tmp[PATH_MAX];
  if (buf == nullptr) {
    buf = tmp;
    size = PATH_MAX;
  } else if (size == 0) {
    errno = EINVAL;
    return nullptr;
  }

  char* out = nullptr;
  {
    fbl::AutoLock lock(&fdio_cwd_lock);
    size_t len = strlen(fdio_cwd_path) + 1;
    if (len < size) {
      memcpy(buf, fdio_cwd_path, len);
      out = buf;
    } else {
      errno = ERANGE;
    }
  }

  if (out == tmp) {
    out = strdup(tmp);
  }
  return out;
}

void fdio_chdir(fdio_t* io, const char* path) {
  fbl::AutoLock cwd_lock(&fdio_cwd_lock);
  update_cwd_path(path);
  fbl::AutoLock lock(&fdio_lock);
  fdio_t* old = fdio_cwd_handle;
  fdio_cwd_handle = io;
  fdio_get_ops(old)->close(old);
  fdio_release(old);
}

__EXPORT
int chdir(const char* path) {
  fdio_t* io;
  zx_status_t r;
  if ((r = __fdio_open(&io, path, O_RDONLY | O_DIRECTORY, 0)) < 0) {
    return STATUS(r);
  }
  fdio_chdir(io, path);
  return 0;
}

struct __dirstream {
  mtx_t lock;

  // fd number of the directory under iteration.
  int fd;

  // The iterator object for reading directory entries.
  zxio_dirent_iterator_t iterator = {};

  // A single directory entry returned to user; updated by |readdir|.
  struct dirent de = {};

  // If |iterator| is initialized. The |iterator| is initialized lazily.
  bool is_iterator_initialized = false;
};

static DIR* internal_opendir(int fd) {
  DIR* dir = new __dirstream;
  mtx_init(&dir->lock, mtx_plain);
  dir->fd = fd;
  return dir;
}

__EXPORT
DIR* opendir(const char* name) {
  int fd = open(name, O_RDONLY | O_DIRECTORY);
  if (fd < 0)
    return nullptr;
  DIR* dir = internal_opendir(fd);
  if (dir == nullptr)
    close(fd);
  return dir;
}

__EXPORT
DIR* fdopendir(int fd) {
  // Check the fd for validity, but we'll just store the fd
  // number so we don't save the fdio_t pointer.
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    errno = EBADF;
    return nullptr;
  }
  // TODO(mcgrathr): Technically this should verify that it's
  // really a directory and fail with ENOTDIR if not.  But
  // that's not so easy to do, so don't bother for now.
  fdio_release(io);
  return internal_opendir(fd);
}

__EXPORT
int closedir(DIR* dir) {
  if (dir->is_iterator_initialized) {
    fdio_t* io = fd_to_io(dir->fd);
    fdio_get_ops(io)->dirent_iterator_destroy(io, &dir->iterator);
    fdio_release(io);
  }
  close(dir->fd);
  delete dir;
  return 0;
}

__EXPORT
struct dirent* readdir(DIR* dir) {
  fbl::AutoLock lock(&dir->lock);
  struct dirent* de = &dir->de;
  zxio_dirent_t* entry = nullptr;

  fdio_t* io = fd_to_io(dir->fd);
  auto clean_io = fbl::MakeAutoCall([io] {
    if (io != nullptr) {
      fdio_release(io);
    }
  });

  // Lazy initialize the iterator.
  if (!dir->is_iterator_initialized) {
    zx_status_t status =
        fdio_get_ops(io)->dirent_iterator_init(io, &dir->iterator, fdio_get_zxio(io));
    if (status != ZX_OK) {
      errno = fdio_status_to_errno(status);
      return nullptr;
    }
    dir->is_iterator_initialized = true;
  }
  zx_status_t status = fdio_get_ops(io)->dirent_iterator_next(io, &dir->iterator, &entry);
  if (status == ZX_ERR_NOT_FOUND) {
    // Reached the end.
    ZX_DEBUG_ASSERT(!entry);
    return nullptr;
  }
  if (status != ZX_OK) {
    errno = fdio_status_to_errno(status);
    return nullptr;
  }
  de->d_ino = entry->has.id ? entry->id : fio::INO_UNKNOWN;
  de->d_off = 0;
  // The d_reclen field is nonstandard, but existing code
  // may expect it to be useful as an upper bound on the
  // length of the name.
  de->d_reclen =
      static_cast<unsigned short>(offsetof(struct dirent, d_name) + entry->name_length + 1);
  if (entry->has.protocols) {
    de->d_type = ([](zxio_node_protocols_t protocols) -> unsigned char {
      if (protocols & ZXIO_NODE_PROTOCOL_DIRECTORY)
        return DT_DIR;
      if (protocols & ZXIO_NODE_PROTOCOL_FILE)
        return DT_REG;
      if (protocols & ZXIO_NODE_PROTOCOL_MEMORY)
        return DT_REG;
      if (protocols & ZXIO_NODE_PROTOCOL_POSIX_SOCKET)
        return DT_SOCK;
      if (protocols & ZXIO_NODE_PROTOCOL_PIPE)
        return DT_FIFO;
      if (protocols & ZXIO_NODE_PROTOCOL_DEVICE)
        return DT_BLK;
      if (protocols & ZXIO_NODE_PROTOCOL_TTY)
        return DT_CHR;
      if (protocols & ZXIO_NODE_PROTOCOL_DEBUGLOG)
        return DT_CHR;
      // There is no good analogue for FIDL services in POSIX land.
      if (protocols & ZXIO_NODE_PROTOCOL_CONNECTOR)
        return DT_UNKNOWN;
      return DT_UNKNOWN;
    })(entry->protocols);
  } else {
    de->d_type = DT_UNKNOWN;
  }
  memcpy(de->d_name, entry->name, entry->name_length);
  de->d_name[entry->name_length] = '\0';
  return de;
}

__EXPORT
void rewinddir(DIR* dir) {
  fbl::AutoLock lock(&dir->lock);
  if (dir->is_iterator_initialized) {
    fdio_t* io = fd_to_io(dir->fd);
    fdio_get_ops(io)->dirent_iterator_destroy(io, &dir->iterator);
    fdio_release(io);
    dir->is_iterator_initialized = false;
  }
}

__EXPORT
int dirfd(DIR* dir) { return dir->fd; }

__EXPORT
int isatty(int fd) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    errno = EBADF;
    return 0;
  }

  bool tty;
  zx_status_t status = zxio_isatty(fdio_get_zxio(io), &tty);

  int ret;
  if ((status == ZX_OK) && tty) {
    ret = 1;
  } else {
    ret = 0;
    errno = ENOTTY;
  }

  fdio_release(io);

  return ret;
}

__EXPORT
mode_t umask(mode_t mask) {
  mode_t oldmask;
  fbl::AutoLock lock(&fdio_lock);
  oldmask = __fdio_global_state.umask;
  __fdio_global_state.umask = mask & 0777;
  return oldmask;
}

__EXPORT
int fdio_handle_fd(zx_handle_t h, zx_signals_t signals_in, zx_signals_t signals_out,
                   bool shared_handle) {
  fdio_t* io = fdio_waitable_create(h, signals_in, signals_out, shared_handle);
  int fd = fdio_bind_to_fd(io, -1, 0);
  if (fd < 0) {
    fdio_close(io);
    fdio_release(io);
  }
  return fd;
}

// from fdio/unsafe.h, to support message-loop integration

__EXPORT
void fdio_unsafe_wait_begin(fdio_t* io, uint32_t events, zx_handle_t* handle_out,
                            zx_signals_t* signals_out) {
  return fdio_get_ops(io)->wait_begin(io, events, handle_out, signals_out);
}

__EXPORT
void fdio_unsafe_wait_end(fdio_t* io, zx_signals_t signals, uint32_t* events_out) {
  return fdio_get_ops(io)->wait_end(io, signals, events_out);
}

__EXPORT
void fdio_unsafe_release(fdio_t* io) { fdio_release(io); }

// TODO: getrlimit(RLIMIT_NOFILE, ...)
#define MAX_POLL_NFDS 1024

__EXPORT
int ppoll(struct pollfd* fds, nfds_t n, const struct timespec* timeout_ts,
          const sigset_t* sigmask) {
  if (sigmask) {
    return ERRNO(ENOSYS);
  }
  if (n > MAX_POLL_NFDS) {
    return ERRNO(EINVAL);
  }

  fdio_t* ios[n];
  nfds_t ios_used_count = 0;

  zx_status_t r = ZX_OK;
  nfds_t nvalid = 0;

  zx_wait_item_t items[n];

  for (nfds_t i = 0; i < n; i++) {
    struct pollfd* pfd = &fds[i];
    pfd->revents = 0;  // initialize to zero

    ios[i] = nullptr;
    if (pfd->fd < 0) {
      // if fd is negative, the entry is invalid
      continue;
    }
    fdio_t* io;
    if ((io = fd_to_io(pfd->fd)) == nullptr) {
      // fd is not opened
      pfd->revents = POLLNVAL;
      continue;
    }
    ios[i] = io;
    ios_used_count = i + 1;

    zx_handle_t h;
    zx_signals_t sigs;
    fdio_get_ops(io)->wait_begin(io, pfd->events, &h, &sigs);
    if (h == ZX_HANDLE_INVALID) {
      // wait operation is not applicable to the handle
      r = ZX_ERR_INVALID_ARGS;
      break;
    }
    items[nvalid].handle = h;
    items[nvalid].waitfor = sigs;
    items[nvalid].pending = 0;
    nvalid++;
  }

  int nfds = 0;
  if (r == ZX_OK) {
    zx_time_t tmo = ZX_TIME_INFINITE;
    // Check for overflows on every operation.
    if (timeout_ts && timeout_ts->tv_sec >= 0 && timeout_ts->tv_nsec >= 0 &&
        timeout_ts->tv_sec <= INT64_MAX / ZX_SEC(1)) {
      zx_duration_t seconds_duration = ZX_SEC(timeout_ts->tv_sec);
      zx_duration_t duration = zx_duration_add_duration(seconds_duration, timeout_ts->tv_nsec);
      if (duration >= seconds_duration) {
        tmo = zx_deadline_after(duration);
      }
    }
    r = zx_object_wait_many(items, nvalid, tmo);
    // pending signals could be reported on ZX_ERR_TIMED_OUT case as well
    if (r == ZX_OK || r == ZX_ERR_TIMED_OUT) {
      nfds_t j = 0;  // j counts up on a valid entry

      for (nfds_t i = 0; i < n; i++) {
        struct pollfd* pfd = &fds[i];
        fdio_t* io = ios[i];

        if (io == nullptr) {
          // skip an invalid entry
          continue;
        }
        if (j < nvalid) {
          uint32_t events = 0;
          fdio_get_ops(io)->wait_end(io, items[j].pending, &events);
          // mask unrequested events except HUP/ERR
          pfd->revents = static_cast<short>(events) & (pfd->events | POLLHUP | POLLERR);
          if (pfd->revents != 0) {
            nfds++;
          }
        }
        j++;
      }
    }
  }

  for (nfds_t i = 0; i < ios_used_count; i++) {
    if (ios[i]) {
      fdio_release(ios[i]);
    }
  }

  return (r == ZX_OK || r == ZX_ERR_TIMED_OUT) ? nfds : ERROR(r);
}

__EXPORT
int poll(struct pollfd* fds, nfds_t n, int timeout) {
  struct timespec timeout_ts = {timeout / 1000, (timeout % 1000) * 1000000};
  struct timespec* ts = timeout >= 0 ? &timeout_ts : nullptr;
  return ppoll(fds, n, ts, nullptr);
}

__EXPORT
int select(int n, fd_set* __restrict rfds, fd_set* __restrict wfds, fd_set* __restrict efds,
           struct timeval* __restrict tv) {
  if (n > FD_SETSIZE || n < 1) {
    return ERRNO(EINVAL);
  }

  fdio_t* ios[n];
  int ios_used_max = -1;

  zx_status_t r = ZX_OK;
  int nvalid = 0;

  zx_wait_item_t items[n];

  for (int fd = 0; fd < n; fd++) {
    ios[fd] = nullptr;

    uint32_t events = 0;
    if (rfds && FD_ISSET(fd, rfds))
      events |= POLLIN;
    if (wfds && FD_ISSET(fd, wfds))
      events |= POLLOUT;
    if (efds && FD_ISSET(fd, efds))
      events |= POLLERR;
    if (events == 0) {
      continue;
    }

    fdio_t* io;
    if ((io = fd_to_io(fd)) == nullptr) {
      r = ZX_ERR_BAD_HANDLE;
      break;
    }
    ios[fd] = io;
    ios_used_max = fd;

    zx_handle_t h;
    zx_signals_t sigs;
    fdio_get_ops(io)->wait_begin(io, events, &h, &sigs);
    if (h == ZX_HANDLE_INVALID) {
      r = ZX_ERR_INVALID_ARGS;
      break;
    }
    items[nvalid].handle = h;
    items[nvalid].waitfor = sigs;
    items[nvalid].pending = 0;
    nvalid++;
  }

  int nfds = 0;
  if (r == ZX_OK && nvalid > 0) {
    zx_time_t tmo =
        (tv == nullptr)
            ? ZX_TIME_INFINITE
            : zx_deadline_after(zx_duration_add_duration(ZX_SEC(tv->tv_sec), ZX_USEC(tv->tv_usec)));
    r = zx_object_wait_many(items, nvalid, tmo);
    // pending signals could be reported on ZX_ERR_TIMED_OUT case as well
    if (r == ZX_OK || r == ZX_ERR_TIMED_OUT) {
      int j = 0;  // j counts up on a valid entry

      for (int fd = 0; fd < n; fd++) {
        fdio_t* io = ios[fd];
        if (io == nullptr) {
          // skip an invalid entry
          continue;
        }
        if (j < nvalid) {
          uint32_t events = 0;
          fdio_get_ops(io)->wait_end(io, items[j].pending, &events);
          if (rfds && FD_ISSET(fd, rfds)) {
            if (events & POLLIN) {
              nfds++;
            } else {
              FD_CLR(fd, rfds);
            }
          }
          if (wfds && FD_ISSET(fd, wfds)) {
            if (events & POLLOUT) {
              nfds++;
            } else {
              FD_CLR(fd, wfds);
            }
          }
          if (efds && FD_ISSET(fd, efds)) {
            if (events & POLLERR) {
              nfds++;
            } else {
              FD_CLR(fd, efds);
            }
          }
        } else {
          if (rfds) {
            FD_CLR(fd, rfds);
          }
          if (wfds) {
            FD_CLR(fd, wfds);
          }
          if (efds) {
            FD_CLR(fd, efds);
          }
        }
        j++;
      }
    }
  }

  for (int i = 0; i <= ios_used_max; i++) {
    if (ios[i]) {
      fdio_release(ios[i]);
    }
  }

  return (r == ZX_OK || r == ZX_ERR_TIMED_OUT) ? nfds : ERROR(r);
}

__EXPORT
int ioctl(int fd, int req, ...) {
  fdio_t* io;
  if ((io = fd_to_io(fd)) == nullptr) {
    return ERRNO(EBADF);
  }

  va_list ap;
  va_start(ap, req);
  zx_status_t r = fdio_get_ops(io)->posix_ioctl(io, req, ap);
  va_end(ap);
  fdio_release(io);
  switch (r) {
    case ZX_ERR_NOT_FOUND:
      return ERRNO(ENODEV);
    case ZX_ERR_NOT_SUPPORTED:
      return ERRNO(ENOTTY);
    default:
      return STATUS(r);
  }
}

__EXPORT
ssize_t sendto(int fd, const void* buf, size_t buflen, int flags, const struct sockaddr* addr,
               socklen_t addrlen) {
  struct iovec iov;
  iov.iov_base = const_cast<void*>(buf);
  iov.iov_len = buflen;

  struct msghdr msg = {};
  msg.msg_name = const_cast<struct sockaddr*>(addr);
  msg.msg_namelen = addrlen;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  return sendmsg(fd, &msg, flags);
}

__EXPORT
ssize_t recvfrom(int fd, void* __restrict buf, size_t buflen, int flags,
                 struct sockaddr* __restrict addr, socklen_t* __restrict addrlen) {
  struct iovec iov;
  iov.iov_base = buf;
  iov.iov_len = buflen;

  struct msghdr msg = {};
  msg.msg_name = addr;
  if (addrlen != nullptr) {
    msg.msg_namelen = *addrlen;
  }
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  ssize_t n = recvmsg(fd, &msg, flags);
  if (addrlen != nullptr) {
    *addrlen = msg.msg_namelen;
  }
  return n;
}

__EXPORT
ssize_t sendmsg(int fd, const struct msghdr* msg, int flags) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  // The |flags| are typically used to express intent *not* to issue SIGPIPE
  // via MSG_NOSIGNAL. Applications use this frequently to avoid having to
  // install additional signal handlers to handle cases where connection has
  // been closed by remote end.
  bool nonblocking = (*fdio_get_ioflag(io) & IOFLAG_NONBLOCK) || (flags & MSG_DONTWAIT);
  flags &= ~MSG_DONTWAIT;
  zx::time deadline = zx::deadline_after(*fdio_get_sndtimeo(io));
  for (;;) {
    size_t actual;
    int16_t out_code;
    zx_status_t status = fdio_get_ops(io)->sendmsg(io, msg, flags, &actual, &out_code);
    if ((status == ZX_ERR_SHOULD_WAIT || (status == ZX_OK && out_code == EWOULDBLOCK)) &&
        !nonblocking) {
      uint32_t pending;
      switch (fdio_wait(io, FDIO_EVT_WRITABLE, deadline, &pending)) {
        case ZX_ERR_BAD_HANDLE:
          status = ZX_ERR_BAD_HANDLE;
          break;
        case ZX_ERR_TIMED_OUT:
          break;
        case ZX_OK:
          if (pending & POLLHUP) {
            status = ZX_ERR_CONNECTION_RESET;
            break;
          }
          __FALLTHROUGH;
        default:
          continue;
      }
    }
    fdio_release(io);
    if (status != ZX_OK) {
      return ERROR(status);
    }
    if (out_code) {
      return ERRNO(out_code);
    }
    return actual;
  }
}

__EXPORT
ssize_t recvmsg(int fd, struct msghdr* msg, int flags) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  bool nonblocking = (*fdio_get_ioflag(io) & IOFLAG_NONBLOCK) || (flags & MSG_DONTWAIT);
  flags &= ~MSG_DONTWAIT;
  zx::time deadline = zx::deadline_after(*fdio_get_rcvtimeo(io));
  for (;;) {
    size_t actual;
    int16_t out_code;
    zx_status_t status = fdio_get_ops(io)->recvmsg(io, msg, flags, &actual, &out_code);
    if ((status == ZX_ERR_SHOULD_WAIT || (status == ZX_OK && out_code == EWOULDBLOCK)) &&
        !nonblocking) {
      uint32_t pending;
      switch (fdio_wait(io, FDIO_EVT_READABLE, deadline, &pending)) {
        case ZX_ERR_BAD_HANDLE:
          status = ZX_ERR_BAD_HANDLE;
          break;
        case ZX_ERR_TIMED_OUT:
          break;
        case ZX_OK:
          if (pending & POLLHUP) {
            status = ZX_ERR_CONNECTION_RESET;
            break;
          }
          __FALLTHROUGH;
        default:
          continue;
      }
    }
    fdio_release(io);
    if (status != ZX_OK) {
      return ERROR(status);
    }
    if (out_code) {
      return ERRNO(out_code);
    }
    return actual;
  }
}

__EXPORT
int shutdown(int fd, int how) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }

  int16_t out_code;
  zx_status_t status = fdio_get_ops(io)->shutdown(io, how, &out_code);
  fdio_release(io);
  if (status != ZX_OK) {
    return ERROR(status);
  }
  if (out_code) {
    return ERRNO(out_code);
  }
  return out_code;
}

// The common denominator between the Linux-y fstatfs and the POSIX
// fstatvfs, which align on most fields. The fs version is more easily
// computed from the fio::FilesystemInfo, so this takes a struct
// statfs.
static int fs_stat(int fd, struct statfs* buf) {
  fdio_t* io;
  if ((io = fd_to_io(fd)) == nullptr) {
    return ERRNO(EBADF);
  }
  zx_handle_t handle = fdio_unsafe_borrow_channel(io);
  if (handle == ZX_HANDLE_INVALID) {
    fdio_release(io);
    return ERRNO(ENOTSUP);
  }
  auto result = fio::DirectoryAdmin::Call::QueryFilesystem(zx::unowned_channel(handle));
  fdio_release(io);
  if (result.status() != ZX_OK) {
    return ERROR(result.status());
  }
  fio::DirectoryAdmin::QueryFilesystemResponse* response = result.Unwrap();
  if (response->s != ZX_OK) {
    return ERROR(response->s);
  }
  fio::FilesystemInfo* info = response->info.get();
  if (info == nullptr) {
    return ERRNO(EIO);
  }

  info->name[fio::MAX_FS_NAME_BUFFER - 1] = '\0';

  struct statfs stats = {};

  if (info->block_size) {
    stats.f_bsize = info->block_size;
    stats.f_blocks = info->total_bytes / stats.f_bsize;
    stats.f_bfree = stats.f_blocks - info->used_bytes / stats.f_bsize;
  }
  stats.f_bavail = stats.f_bfree;
  stats.f_files = info->total_nodes;
  stats.f_ffree = info->total_nodes - info->used_nodes;
  stats.f_namelen = info->max_filename_size;
  stats.f_type = info->fs_type;
  stats.f_fsid.__val[0] = static_cast<int>(info->fs_id & 0xffffffff);
  stats.f_fsid.__val[1] = static_cast<int>(info->fs_id >> 32u);

  *buf = stats;
  return 0;
}

__EXPORT
int fstatfs(int fd, struct statfs* buf) { return fs_stat(fd, buf); }

__EXPORT
int statfs(const char* path, struct statfs* buf) {
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return fd;
  }
  int rv = fstatfs(fd, buf);
  close(fd);
  return rv;
}

__EXPORT
int fstatvfs(int fd, struct statvfs* buf) {
  struct statfs stats = {};
  int result = fs_stat(fd, &stats);
  if (result >= 0) {
    struct statvfs vstats = {};

    // The following fields are 1-1 between the Linux statfs
    // definition and the POSIX statvfs definition.
    vstats.f_bsize = stats.f_bsize;
    vstats.f_blocks = stats.f_blocks;
    vstats.f_bfree = stats.f_bfree;
    vstats.f_bavail = stats.f_bavail;

    vstats.f_files = stats.f_files;
    vstats.f_ffree = stats.f_ffree;

    vstats.f_flag = stats.f_flags;

    vstats.f_namemax = stats.f_namelen;

    // The following fields have slightly different semantics
    // between the two.

    // The two have different representations for the fsid.
    vstats.f_fsid = stats.f_fsid.__val[0] + (((uint64_t)stats.f_fsid.__val[1]) << 32);

    // The statvfs "fragment size" value best corresponds to the
    // FilesystemInfo "block size" value.
    vstats.f_frsize = stats.f_bsize;

    // The statvfs struct distinguishes between available files,
    // and available files for unprivileged processes. fuchsia.io
    // makes no such distinction, so use the same value for both.
    vstats.f_favail = stats.f_ffree;

    // Finally, the f_type and f_spare fields on struct statfs
    // have no equivalent for struct statvfs.

    *buf = vstats;
  }
  return result;
}

__EXPORT
int statvfs(const char* path, struct statvfs* buf) {
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return fd;
  }
  int rv = fstatvfs(fd, buf);
  close(fd);
  return rv;
}

// extern "C" is required here, since the corresponding declaration is in an internal musl header:
// zircon/third_party/ulib/musl/src/internal/libc.h
extern "C" __EXPORT int _fd_open_max(void) { return FDIO_MAX_FD; }
