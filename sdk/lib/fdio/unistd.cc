// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/private.h>
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
#include <thread>
#include <utility>

#include <fbl/auto_lock.h>

#include "fdio_unistd.h"
#include "internal.h"
#include "zxio.h"

namespace fio = fuchsia_io;

static_assert(IOFLAG_CLOEXEC == FD_CLOEXEC, "Unexpected fdio flags value");

// non-thread-safe emulation of unistd io functions using the fdio transports

// Constexpr function to force initialization at program load time. Otherwise initialization may
// occur after |__libc_extension_init|, wiping the fd table *after* it has been filled in with valid
// entries; musl invokes |__libc_start_init| after |__libc_extensions_init|.
//
// There are no language guarantees here. C++20 provides the constinit specifier, and clang provides
// the [[clang::require_constant_initialization]] attribute, but until we are on C++20 this is the
// best we can do.
//
// Note that even moving the initialization to |__libc_extensions_init| doesn't work out in the
// presence of sanitizers that deliberately initialize with garbage *after* |__libc_extensions_init|
// runs.
fdio_state_t __fdio_global_state = []() constexpr {
  return fdio_state_t{
      .cwd_path = "/",
  };
}
();

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
fdio_ptr fdio_iodir(const char** path, int dirfd) {
  bool root = *path[0] == '/';
  if (root) {
    // Since we are sending a request to the root handle, the
    // rest of the path should be canonicalized as a relative
    // path (relative to this root handle).
    while (**path == '/') {
      (*path)++;
      if (**path == 0) {
        *path = ".";
      }
    }
  }
  fbl::AutoLock lock(&fdio_lock);
  if (root) {
    return fdio_root_handle.get();
  }
  if (dirfd == AT_FDCWD) {
    return fdio_cwd_handle.get();
  }
  return fd_to_io_locked(dirfd);
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

namespace fdio_internal {

zx::status<fdio_ptr> open_at_impl(int dirfd, const char* path, int flags, uint32_t mode,
                                  bool enforce_eisdir) {
  if (path == nullptr) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (path[0] == '\0') {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  fdio_ptr iodir = fdio_iodir(&path, dirfd);
  if (iodir == nullptr) {
    return zx::error(ZX_ERR_BAD_HANDLE);
  }

  char clean[PATH_MAX];
  size_t outlen;
  bool has_ending_slash;
  zx_status_t status = __fdio_cleanpath(path, clean, &outlen, &has_ending_slash);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  // Emulate EISDIR behavior from
  // http://pubs.opengroup.org/onlinepubs/9699919799/functions/open.html
  bool flags_incompatible_with_directory =
      ((flags & ~O_PATH & O_ACCMODE) != O_RDONLY) || (flags & O_CREAT);
  if (enforce_eisdir && has_ending_slash && flags_incompatible_with_directory) {
    return zx::error(ZX_ERR_NOT_FILE);
  }
  flags |= (has_ending_slash ? O_DIRECTORY : 0);

  uint32_t zx_flags = fdio_flags_to_zxio(static_cast<uint32_t>(flags));

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
  return iodir->open(clean, zx_flags, mode);
}

// Open |path| from the |dirfd| directory, enforcing the POSIX EISDIR error condition. Specifically,
// ZX_ERR_NOT_FILE will be returned when opening a directory with write access/O_CREAT.
zx::status<fdio_ptr> open_at(int dirfd, const char* path, int flags, uint32_t mode) {
  return open_at_impl(dirfd, path, flags, mode, true);
}

// Open |path| from the |dirfd| directory, but allow creating directories/opening them with
// write access. Note that this differs from POSIX behavior.
zx::status<fdio_ptr> open_at_ignore_eisdir(int dirfd, const char* path, int flags, uint32_t mode) {
  return open_at_impl(dirfd, path, flags, mode, false);
}

// Open |path| from the current working directory, respecting EISDIR.
zx::status<fdio_ptr> open(const char* path, int flags, uint32_t mode) {
  return open_at(AT_FDCWD, path, flags, mode);
}

void update_cwd_path(const char* path) __TA_REQUIRES(fdio_cwd_lock) {
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
// Returns the last component of the path in `out`, which must be a buffer that can fit [NAME_MAX +
// 1] characters.  If `is_dir_out` is nullptr, a trailing slash will be added to the name
// if the last component happens to be a directory.  Otherwise, `is_dir_out` will be set to indicate
// whether the last component is a directory.
zx::status<fdio_ptr> opendir_containing_at(int dirfd, const char* path, char* out,
                                           bool* is_dir_out) {
  if (path == nullptr) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  fdio_ptr iodir = fdio_iodir(&path, dirfd);
  if (iodir == nullptr) {
    return zx::error(ZX_ERR_BAD_HANDLE);
  }

  char clean[PATH_MAX];
  size_t pathlen;
  bool is_dir;
  zx_status_t status = __fdio_cleanpath(path, clean, &pathlen, &is_dir);
  if (status != ZX_OK) {
    return zx::error(status);
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
    return zx::error(ZX_ERR_BAD_PATH);
  }

  // Copy the trailing 'name' to out.
  memcpy(out, clean + i, namelen);
  if (is_dir_out) {
    *is_dir_out = is_dir;
  } else if (is_dir) {
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

  return iodir->open(clean, fdio_flags_to_zxio(O_RDONLY | O_DIRECTORY), 0);
}

}  // namespace fdio_internal

// hook into libc process startup
// this is called prior to main to set up the fdio world
// and thus does not use the fdio_lock
//
// extern "C" is required here, since the corresponding declaration is in an internal musl header:
// zircon/third_party/ulib/musl/src/internal/libc.h
extern "C" __EXPORT void __libc_extensions_init(uint32_t handle_count, zx_handle_t handle[],
                                                uint32_t handle_info[], uint32_t name_count,
                                                char** names) __TA_NO_THREAD_SAFETY_ANALYSIS {
  {
    zx_status_t status = fdio_ns_create(&fdio_root_ns);
    ZX_ASSERT_MSG(status == ZX_OK, "Failed to create root namespace: %s",
                  zx_status_get_string(status));
  }

  fdio_ptr use_for_stdio = nullptr;

  // extract handles we care about
  for (uint32_t n = 0; n < handle_count; n++) {
    unsigned arg = PA_HND_ARG(handle_info[n]);
    zx_handle_t h = handle[n];

    // precalculate the fd from |arg|, for FDIO cases to use.
    unsigned arg_fd = arg & (~FDIO_FLAG_USE_FOR_STDIO);

    switch (PA_HND_TYPE(handle_info[n])) {
      case PA_FD: {
        zx::status io = fdio::create(zx::handle(h));
        if (io.is_error()) {
          continue;
        }
        ZX_ASSERT_MSG(arg_fd < FDIO_MAX_FD,
                      "unreasonably large fd number %u in PA_FD (must be less than %u)", arg_fd,
                      FDIO_MAX_FD);
        ZX_ASSERT_MSG(fdio_fdtab[arg_fd].try_set(io.value()), "duplicate fd number %u in PA_FD",
                      arg_fd);

        if (arg & FDIO_FLAG_USE_FOR_STDIO) {
          use_for_stdio = std::move(io.value());
        }

        handle[n] = 0;
        handle_info[n] = 0;

        break;
      }
      case PA_NS_DIR:
        if (arg < name_count) {
          fdio_ns_bind(fdio_root_ns, names[arg], h);
        }
        // we always continue here to not steal the
        // handles from higher level code that may
        // also need access to the namespace
        continue;
      default:
        // unknown handle, leave it alone
        continue;
    }
  }

  {
    const char* cwd = getenv("PWD");
    fdio_internal::update_cwd_path(cwd ? cwd : "/");
  }

  fdio_ptr null_io = nullptr;
  auto get_null = [&null_io]() {
    if (null_io == nullptr) {
      zx::status null = fdio_internal::zxio::create();
      ZX_ASSERT_MSG(null.is_ok(), "%s", null.status_string());
      null_io = std::move(null.value());
    }
    return null_io;
  };

  if (use_for_stdio == nullptr) {
    use_for_stdio = get_null();
  }

  // configure stdin/out/err if not init'd
  for (uint32_t n = 0; n < 3; n++) {
    fdio_fdtab[n].try_set(use_for_stdio);
  }

  zx::status root = fdio_ns_open_root(fdio_root_ns);
  if (root.is_ok()) {
    ZX_ASSERT(fdio_root_handle.try_set(root.value()));
    zx::status cwd = fdio_internal::open(fdio_cwd_path, O_RDONLY | O_DIRECTORY, 0);
    if (cwd.is_ok()) {
      ZX_ASSERT(fdio_cwd_handle.try_set(cwd.value()));
    } else {
      ZX_ASSERT(fdio_cwd_handle.try_set(get_null()));
    }
  } else {
    ZX_ASSERT(fdio_root_handle.try_set(get_null()));
    ZX_ASSERT(fdio_cwd_handle.try_set(get_null()));
  }
}

// Clean up during process teardown. This runs after atexit hooks in
// libc. It continues to hold the fdio lock until process exit, to
// prevent other threads from racing on file descriptors.
//
// extern "C" is required here, since the corresponding declaration is in an internal musl header:
// zircon/third_party/ulib/musl/src/internal/libc.h
extern "C" __EXPORT void __libc_extensions_fini(void) __TA_ACQUIRE(fdio_lock) {
  mtx_lock(&fdio_lock);
  __UNUSED auto root = fdio_root_handle.release();
  __UNUSED auto cwd = fdio_cwd_handle.release();
  for (auto& var : fdio_fdtab) {
    __UNUSED fdio_ptr io = var.release();
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

zx_status_t fdio_wait(const fdio_ptr& io, uint32_t events, zx::time deadline,
                      uint32_t* out_pending) {
  zx_handle_t h = ZX_HANDLE_INVALID;
  zx_signals_t signals = 0;
  io->wait_begin(events, &h, &signals);
  if (h == ZX_HANDLE_INVALID) {
    // Wait operation is not applicable to the handle.
    return ZX_ERR_WRONG_TYPE;
  }

  zx_signals_t pending;
  zx_status_t status = zx_object_wait_one(h, signals, deadline.get(), &pending);
  if (status == ZX_OK || status == ZX_ERR_TIMED_OUT) {
    io->wait_end(pending, &events);
    if (out_pending != nullptr) {
      *out_pending = events;
    }
  }

  return status;
}

static zx_status_t fdio_stat(const fdio_ptr& io, struct stat* s) {
  zxio_node_attributes_t attr;
  zx_status_t status = io->get_attr(&attr);
  if (status != ZX_OK) {
    return status;
  }

  memset(s, 0, sizeof(struct stat));
  s->st_mode = io->convert_to_posix_mode(attr.protocols, attr.abilities);
  s->st_ino = attr.has.id ? attr.id : fio::wire::kInoUnknown;
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

// The functions from here on provide implementations of fd and path
// centric posix-y io operations.

// extern "C" is required here, since the corresponding declaration is in an internal musl header:
// zircon/third_party/ulib/musl/src/internal/stdio_impl.h
extern "C" __EXPORT zx_status_t _mmap_file(size_t offset, size_t len, zx_vm_option_t zx_options,
                                           int flags, int fd, off_t fd_off, uintptr_t* out) {
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ZX_ERR_BAD_HANDLE;
  }

  int vflags = zx_options | (flags & MAP_PRIVATE ? fio::wire::kVmoFlagPrivate : 0);

  zx::vmo vmo;
  size_t size;
  {
    zx_status_t status =
        zxio_vmo_get(&io->zxio_storage().io, vflags, vmo.reset_and_get_address(), &size);
    if (status != ZX_OK) {
      // On POSIX, performing mmap on an fd which does not support it returns an access denied
      // error.
      if (status == ZX_ERR_NOT_SUPPORTED) {
        status = ZX_ERR_ACCESS_DENIED;
      }
      return status;
    }
  }

  uintptr_t ptr = 0;
  zx_options |= ZX_VM_ALLOW_FAULTS;
  zx_status_t status =
      zx_vmar_map(zx_vmar_root_self(), zx_options, offset, vmo.get(), fd_off, len, &ptr);
  // TODO: map this as shared if we ever implement forking
  if (status != ZX_OK) {
    return status;
  }

  *out = ptr;
  return ZX_OK;
}

__EXPORT
int unlinkat(int dirfd, const char* path, int flags) {
  char name[NAME_MAX + 1];
  bool is_dir;
  zx::status io = fdio_internal::opendir_containing_at(dirfd, path, name, &is_dir);
  if (io.is_error()) {
    return ERROR(io.status_value());
  }
  if (is_dir) {
    flags |= AT_REMOVEDIR;
  }
  return STATUS(io->unlink(name, strlen(name), flags));
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
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  bool nonblocking = io->ioflag() & IOFLAG_NONBLOCK;
  zx::time deadline = zx::deadline_after(io->rcvtimeo());

  zx_iovec_t zx_iov[iovcnt];
  for (int i = 0; i < iovcnt; ++i) {
    zx_iov[i] = {
        .buffer = iov[i].iov_base,
        .capacity = iov[i].iov_len,
    };
  }

  for (;;) {
    size_t actual;
    zx_status_t status = zxio_readv_at(&io->zxio_storage().io, offset, zx_iov, iovcnt, 0, &actual);
    if (status == ZX_ERR_SHOULD_WAIT && !nonblocking) {
      status = fdio_wait(io, FDIO_EVT_READABLE, deadline, nullptr);
      if (status == ZX_OK) {
        continue;
      }
      if (status == ZX_ERR_TIMED_OUT) {
        status = ZX_ERR_SHOULD_WAIT;
      }
    }
    if (status != ZX_OK) {
      return ERROR(status);
    }
    return actual;
  }
}

__EXPORT
ssize_t pwritev(int fd, const struct iovec* iov, int iovcnt, off_t offset) {
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  bool nonblocking = io->ioflag() & IOFLAG_NONBLOCK;
  zx::time deadline = zx::deadline_after(io->sndtimeo());

  zx_iovec_t zx_iov[iovcnt];
  for (int i = 0; i < iovcnt; ++i) {
    zx_iov[i] = {
        .buffer = iov[i].iov_base,
        .capacity = iov[i].iov_len,
    };
  }

  for (;;) {
    size_t actual;
    zx_status_t status = zxio_writev_at(&io->zxio_storage().io, offset, zx_iov, iovcnt, 0, &actual);
    if (status == ZX_ERR_SHOULD_WAIT && !nonblocking) {
      status = fdio_wait(io, FDIO_EVT_WRITABLE, deadline, nullptr);
      if (status == ZX_OK) {
        continue;
      }
      if (status == ZX_ERR_TIMED_OUT) {
        status = ZX_ERR_SHOULD_WAIT;
      }
    }
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
  fdio_ptr io = unbind_from_fd(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  std::optional ptr = GetLastReference(std::move(io));
  if (ptr.has_value()) {
    return STATUS(ptr.value().Close());
  }
  return 0;
}

__EXPORT
int dup2(int oldfd, int newfd) {
  if (newfd < 0 || newfd >= FDIO_MAX_FD) {
    return ERRNO(EBADF);
  }
  // Don't release under lock.
  fdio_ptr io_to_close = nullptr;
  {
    fbl::AutoLock lock(&fdio_lock);
    fdio_ptr io = fd_to_io_locked(oldfd);
    if (io == nullptr) {
      return ERRNO(EBADF);
    }
    io_to_close = fdio_fdtab[newfd].replace(io);
  }
  return newfd;
}

__EXPORT
int dup(int oldfd) {
  fbl::AutoLock lock(&fdio_lock);
  fdio_ptr io = fd_to_io_locked(oldfd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  std::optional fd = bind_to_fd_locked(io);
  if (fd.has_value()) {
    return fd.value();
  }
  return ERRNO(EMFILE);
}

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

  // TODO(fxbug.dev/30920) Implement O_CLOEXEC.
  return dup2(oldfd, newfd);
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
      // TODO(fxbug.dev/30920) Implement CLOEXEC.
      GET_INT_ARG(starting_fd);
      if (starting_fd < 0) {
        return ERRNO(EINVAL);
      }
      fbl::AutoLock lock(&fdio_lock);
      fdio_ptr io = fd_to_io_locked(fd);
      if (io == nullptr) {
        return ERRNO(EBADF);
      }
      for (fd = starting_fd; fd < FDIO_MAX_FD; fd++) {
        if (fdio_fdtab[fd].try_set(io)) {
          return fd;
        }
      }
      return ERRNO(EMFILE);
    }
    case F_GETFD: {
      fdio_ptr io = fd_to_io(fd);
      if (io == nullptr) {
        return ERRNO(EBADF);
      }
      int flags = static_cast<int>(io->ioflag() & IOFLAG_FD_FLAGS);
      // POSIX mandates that the return value be nonnegative if successful.
      assert(flags >= 0);
      return flags;
    }
    case F_SETFD: {
      fdio_ptr io = fd_to_io(fd);
      if (io == nullptr) {
        return ERRNO(EBADF);
      }
      GET_INT_ARG(flags);
      // TODO(fxbug.dev/30920) Implement CLOEXEC.
      io->ioflag() &= ~IOFLAG_FD_FLAGS;
      io->ioflag() |= static_cast<uint32_t>(flags) & IOFLAG_FD_FLAGS;
      return 0;
    }
    case F_GETFL: {
      fdio_ptr io = fd_to_io(fd);
      if (io == nullptr) {
        return ERRNO(EBADF);
      }
      uint32_t flags = 0;
      zx_status_t r = io->get_flags(&flags);
      if (r == ZX_ERR_NOT_SUPPORTED) {
        // We treat this as non-fatal, as it's valid for a remote to
        // simply not support FCNTL, but we still want to correctly
        // report the state of the (local) NONBLOCK flag
        flags = 0;
        r = ZX_OK;
      }
      flags = zxio_flags_to_fdio(flags);
      if (io->ioflag() & IOFLAG_NONBLOCK) {
        flags |= O_NONBLOCK;
      }
      if (r != ZX_OK) {
        return ERROR(r);
      }
      return flags;
    }
    case F_SETFL: {
      fdio_ptr io = fd_to_io(fd);
      if (io == nullptr) {
        return ERRNO(EBADF);
      }
      GET_INT_ARG(n);

      zx_status_t r;
      uint32_t flags = fdio_flags_to_zxio(n & ~O_NONBLOCK);
      r = io->set_flags(flags);

      // Some remotes don't support setting flags; we
      // can adjust their local flags anyway if NONBLOCK
      // is the only bit being toggled.
      if (r == ZX_ERR_NOT_SUPPORTED && ((n | O_NONBLOCK) == O_NONBLOCK)) {
        r = ZX_OK;
      }

      if (r != ZX_OK) {
        n = ERROR(r);
      } else {
        if (n & O_NONBLOCK) {
          io->ioflag() |= IOFLAG_NONBLOCK;
        } else {
          io->ioflag() &= ~IOFLAG_NONBLOCK;
        }
        n = 0;
      }
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
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }

  static_assert(SEEK_SET == ZXIO_SEEK_ORIGIN_START);
  static_assert(SEEK_CUR == ZXIO_SEEK_ORIGIN_CURRENT);
  static_assert(SEEK_END == ZXIO_SEEK_ORIGIN_END);

  size_t result = 0u;
  zx_status_t status = zxio_seek(&io->zxio_storage().io, whence, offset, &result);
  if (status == ZX_ERR_WRONG_TYPE) {
    // Although 'ESPIPE' is a bit of a misnomer, it is the valid errno
    // for any fd which does not implement seeking (i.e., for pipes,
    // sockets, etc).
    return ERRNO(ESPIPE);
  }
  return status != ZX_OK ? ERROR(status) : static_cast<off_t>(result);
}

static int truncateat(int dirfd, const char* path, off_t len) {
  zx::status io = fdio_internal::open_at(dirfd, path, O_WRONLY, 0);
  if (io.is_error()) {
    return ERROR(io.status_value());
  }
  if (len < 0) {
    return ERRNO(EINVAL);
  }
  return STATUS(io->truncate(static_cast<uint64_t>(len)));
}

__EXPORT
int truncate(const char* path, off_t len) { return truncateat(AT_FDCWD, path, len); }

__EXPORT
int ftruncate(int fd, off_t len) {
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  if (len < 0) {
    return ERRNO(EINVAL);
  }
  return STATUS(io->truncate(static_cast<uint64_t>(len)));
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
                          two_path_op fdio_t::*op_getter) {
  char oldname[NAME_MAX + 1];
  zx::status io_oldparent =
      fdio_internal::opendir_containing_at(olddirfd, oldpath, oldname, nullptr);
  if (io_oldparent.is_error()) {
    return ERROR(io_oldparent.status_value());
  }

  char newname[NAME_MAX + 1];
  zx::status io_newparent =
      fdio_internal::opendir_containing_at(newdirfd, newpath, newname, nullptr);
  if (io_newparent.is_error()) {
    return ERROR(io_newparent.status_value());
  }

  zx_handle_t token;
  zx_status_t status = io_newparent->get_token(&token);
  if (status != ZX_OK) {
    return ERROR(status);
  }
  return STATUS((io_oldparent.value().get()->*op_getter)(oldname, strlen(oldname), token, newname,
                                                         strlen(newname)));
}

__EXPORT
int renameat(int olddirfd, const char* oldpath, int newdirfd, const char* newpath) {
  return two_path_op_at(olddirfd, oldpath, newdirfd, newpath, &fdio_t::rename);
}

__EXPORT
int rename(const char* oldpath, const char* newpath) {
  return two_path_op_at(AT_FDCWD, oldpath, AT_FDCWD, newpath, &fdio_t::rename);
}

__EXPORT
int link(const char* oldpath, const char* newpath) {
  return two_path_op_at(AT_FDCWD, oldpath, AT_FDCWD, newpath, &fdio_t::link);
}

__EXPORT
int unlink(const char* path) { return unlinkat(AT_FDCWD, path, 0); }

static int vopenat(int dirfd, const char* path, int flags, va_list args) {
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
  zx::status io = fdio_internal::open_at(dirfd, path, flags, mode);
  if (io.is_error()) {
    return ERROR(io.status_value());
  }
  if (flags & O_NONBLOCK) {
    io->ioflag() |= IOFLAG_NONBLOCK;
  }
  std::optional fd = bind_to_fd(io.value());
  if (fd.has_value()) {
    return fd.value();
  }
  return ERRNO(EMFILE);
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
  mode = (mode & 0777) | S_IFDIR;

  return STATUS(fdio_internal::open_at_ignore_eisdir(dirfd, path, O_RDONLY | O_CREAT | O_EXCL, mode)
                    .status_value());
}

__EXPORT
int fsync(int fd) {
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  return STATUS(zxio_sync(&io->zxio_storage().io));
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
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  return STATUS(fdio_stat(io, s));
}

__EXPORT
int fstatat(int dirfd, const char* fn, struct stat* s, int flags) {
  zx::status io = fdio_internal::open_at(dirfd, fn, O_PATH, 0);
  if (io.is_error()) {
    return ERROR(io.status_value());
  }
  return STATUS(fdio_stat(io.value(), s));
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

static zx_status_t zx_utimens(const fdio_ptr& io, const std::timespec times[2], int flags) {
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
  return io->set_attr(&attr);
}

__EXPORT
int utimensat(int dirfd, const char* path, const struct timespec times[2], int flags) {
  // TODO(orr): AT_SYMLINK_NOFOLLOW
  if ((flags & AT_SYMLINK_NOFOLLOW) != 0) {
    // Allow this flag - don't return an error.  Fuchsia does not support
    // symlinks, so don't break utilities (like tar) that use this flag.
  }
  zx::status io = fdio_internal::open_at_ignore_eisdir(dirfd, path, O_WRONLY, 0);
  if (io.is_error()) {
    return ERROR(io.status_value());
  }
  return STATUS(zx_utimens(io.value(), times, 0));
}

__EXPORT
int futimens(int fd, const struct timespec times[2]) {
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  return STATUS(zx_utimens(io, times, 0));
}

static int socketpair_create(int fd[2], uint32_t options) {
  zx::status pair = fdio_internal::pipe::create_pair(options);
  if (pair.is_error()) {
    return ERROR(pair.status_value());
  }
  auto [left, right] = pair.value();
  std::array<fdio_ptr, 2> ios = {left, right};

  size_t n = 0;

  fbl::AutoLock lock(&fdio_lock);
  for (size_t i = 0; i < fdio_fdtab.size(); ++i) {
    if (fdio_fdtab[i].try_set(ios[n])) {
      fd[n] = i;
      n++;
      if (n == 2) {
        return 0;
      }
    }
  }
  return ERRNO(EMFILE);
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

  if (amode == F_OK) {
    // Check that the file exists a la fstatat.
    zx::status io = fdio_internal::open_at(dirfd, filename, O_PATH, 0);
    if (io.is_error()) {
      return ERROR(io.status_value());
    }
    struct stat s;
    return STATUS(fdio_stat(io.value(), &s));
  }

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
  return STATUS(
      fdio_internal::open_at_ignore_eisdir(dirfd, filename, rights_flags, 0).status_value());
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

void fdio_chdir(fdio_ptr io, const char* path) {
  fbl::AutoLock cwd_lock(&fdio_cwd_lock);
  fdio_internal::update_cwd_path(path);
  fbl::AutoLock lock(&fdio_lock);
  fdio_cwd_handle.replace(std::move(io));
}

__EXPORT
int chdir(const char* path) {
  zx::status io = fdio_internal::open(path, O_RDONLY | O_DIRECTORY, 0);
  if (io.is_error()) {
    return ERROR(io.status_value());
  }
  fdio_chdir(io.value(), path);
  return 0;
}

static zx_status_t resolve_path(const char* relative, char* out_resolved, size_t* out_length) {
  bool is_dir = false;
  if (relative[0] == '/') {
    return __fdio_cleanpath(relative, out_resolved, out_length, &is_dir);
  }

  char buffer[PATH_MAX] = {};
  {
    fbl::AutoLock cwd_lock(&fdio_cwd_lock);
    strcpy(buffer, fdio_cwd_path);
  }
  size_t cwd_length = strlen(buffer);
  size_t relative_length = strlen(relative);

  if (cwd_length + relative_length + 2 > PATH_MAX) {
    return ZX_ERR_BAD_PATH;
  }

  buffer[cwd_length] = '/';
  memcpy(buffer + cwd_length + 1, relative, relative_length + 1);
  return __fdio_cleanpath(buffer, out_resolved, out_length, &is_dir);
}

__EXPORT
int chroot(const char* path) {
  char root_path[PATH_MAX];
  size_t root_path_length = 0u;
  zx_status_t status = resolve_path(path, root_path, &root_path_length);
  if (status != ZX_OK) {
    return ERROR(status);
  }

  zx::status io = fdio_internal::open(root_path, O_RDONLY | O_DIRECTORY, 0);
  if (io.is_error()) {
    return ERROR(io.status_value());
  }

  // Don't release under lock.
  fdio_ptr old_root = nullptr;
  {
    // We acquire the |cwd_lock| after calling |fdio_internal::open| because we cannot hold this
    // lock for the duration of the |fdio_internal::open| call. We are careful to pass an absolute
    // path to |fdio_internal::open| to ensure that we're using a consistent value for the |cwd|
    // throughout the |chroot| operation. If there is a concurrent call to |chdir| during the
    // |fdio_internal::open| operation, then we could end up in an inconsistent state, but the only
    // inconsistency would be the name we apply to the cwd session in the new chrooted namespace.
    fbl::AutoLock cwd_lock(&fdio_cwd_lock);
    fbl::AutoLock lock(&fdio_lock);

    status = fdio_ns_set_root(fdio_root_ns, io.value().get());
    if (status != ZX_OK) {
      return ERROR(status);
    }
    old_root = fdio_root_handle.replace(io.value());

    // We are now committed to the root.

    // If the new root path is a prefix of the cwd path, then we can express the current cwd as a
    // path in the new root by trimming off the prefix. Otherwise, we no longer have a name for the
    // cwd.
    if (root_path_length > 1) {
      std::string_view cwd_view(fdio_cwd_path);
      if (cwd_view.find(root_path) == 0u && fdio_cwd_path[root_path_length] == '/') {
        cwd_view.remove_prefix(root_path_length);
        memmove(fdio_cwd_path, cwd_view.data(), cwd_view.length() + 1);
      } else {
        strcpy(fdio_cwd_path, "(unreachable)");
      }
    }
  }

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
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    errno = EBADF;
    return nullptr;
  }
  // TODO(mcgrathr): Technically this should verify that it's
  // really a directory and fail with ENOTDIR if not.  But
  // that's not so easy to do, so don't bother for now.
  return internal_opendir(fd);
}

__EXPORT
int closedir(DIR* dir) {
  if (dir->is_iterator_initialized) {
    fdio_ptr io = fd_to_io(dir->fd);
    io->dirent_iterator_destroy(&dir->iterator);
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

  fdio_ptr io = fd_to_io(dir->fd);

  // Lazy initialize the iterator.
  if (!dir->is_iterator_initialized) {
    zx_status_t status = io->dirent_iterator_init(&dir->iterator, &io->zxio_storage().io);
    if (status != ZX_OK) {
      errno = fdio_status_to_errno(status);
      return nullptr;
    }
    dir->is_iterator_initialized = true;
  }
  zx_status_t status = io->dirent_iterator_next(&dir->iterator, &entry);
  if (status == ZX_ERR_NOT_FOUND) {
    // Reached the end.
    ZX_DEBUG_ASSERT(!entry);
    return nullptr;
  }
  if (status != ZX_OK) {
    errno = fdio_status_to_errno(status);
    return nullptr;
  }
  de->d_ino = entry->has.id ? entry->id : fio::wire::kInoUnknown;
  de->d_off = 0;
  // The d_reclen field is nonstandard, but existing code
  // may expect it to be useful as an upper bound on the
  // length of the name.
  de->d_reclen = static_cast<uint16_t>(offsetof(struct dirent, d_name) + entry->name_length + 1);
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
    fdio_ptr io = fd_to_io(dir->fd);
    io->dirent_iterator_destroy(&dir->iterator);
    dir->is_iterator_initialized = false;
  }
}

__EXPORT
int dirfd(DIR* dir) { return dir->fd; }

__EXPORT
int isatty(int fd) {
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    errno = EBADF;
    return 0;
  }

  bool tty;
  zx_status_t status = zxio_isatty(&io->zxio_storage().io, &tty);
  if (status != ZX_OK) {
    return ERROR(status);
  }
  if (tty) {
    return 1;
  }
  errno = ENOTTY;
  return 0;
}

__EXPORT
mode_t umask(mode_t mask) {
  mode_t oldmask;
  fbl::AutoLock lock(&fdio_lock);
  oldmask = __fdio_global_state.umask;
  __fdio_global_state.umask = mask & 0777;
  return oldmask;
}

// TODO: getrlimit(RLIMIT_NOFILE, ...)
#define MAX_POLL_NFDS 1024

__EXPORT
int ppoll(struct pollfd* fds, nfds_t n, const struct timespec* timeout_ts,
          const sigset_t* sigmask) {
  if (sigmask) {
    return ERRNO(ENOSYS);
  }
  if (n > MAX_POLL_NFDS || n < 0) {
    return ERRNO(EINVAL);
  }

  auto timeout = zx::duration::infinite();
  if (timeout_ts) {
    // Match Linux's validation strategy. See:
    //
    // https://github.com/torvalds/linux/blob/f40ddce/include/linux/time64.h#L84-L96
    //
    // https://github.com/torvalds/linux/blob/f40ddce/include/vdso/time64.h#L11
    if (timeout_ts->tv_sec < 0 || timeout_ts->tv_nsec < 0 || timeout_ts->tv_nsec >= 1000000000L) {
      return ERRNO(EINVAL);
    }
    timeout = zx::duration(*timeout_ts);
  }

  if (n == 0) {
    std::this_thread::sleep_for(std::chrono::nanoseconds(timeout.to_nsecs()));
    return 0;
  }

  // TODO(https://fxbug.dev/71558): investigate VLA alternatives.
  fdio_ptr ios[n];
  // |items| is the set of handles to wait on and will contain up to |n| entries. Some
  // FDs do not contain a handle or do not have any applicable Zircon signals, so we
  // won't populate an entry in |items| for these FDs. Thus |items| may have fewer
  // entries than |n|.
  zx_wait_item_t items[n];
  // |nitems| tracks the number of populated entries in |items|.
  size_t nitems = 0;
  // |items_set| keeps track of which entries in |fds| have a corresponding
  // entry in |items|. It is true for FDs that have an entry in |items|.
  bool items_set[n];

  for (nfds_t i = 0; i < n; ++i) {
    auto& pfd = fds[i];
    auto& io = ios[i];
    if ((io = fd_to_io(pfd.fd)) == nullptr) {
      // fd is not opened
      pfd.revents = POLLNVAL;
      items_set[i] = false;
      continue;
    }

    zx_handle_t h = ZX_HANDLE_INVALID;
    zx_signals_t sigs = ZX_SIGNAL_NONE;
    io->wait_begin(pfd.events, &h, &sigs);
    if (sigs == ZX_SIGNAL_NONE) {
      // Skip waiting on this fd as there are no waitable signals.
      uint32_t events;
      io->wait_end(sigs, &events);
      pfd.revents = static_cast<int16_t>(events);
      items_set[i] = false;
      continue;
    }
    if (h == ZX_HANDLE_INVALID) {
      return ERROR(ZX_ERR_INVALID_ARGS);
    }
    pfd.revents = 0;
    items[nitems] = {
        .handle = h,
        .waitfor = sigs,
    };
    items_set[i] = true;
    ++nitems;
  }

  if (nitems != 0) {
    zx_status_t status = zx::handle::wait_many(items, nitems, zx::deadline_after(timeout));
    // pending signals could be reported on ZX_ERR_TIMED_OUT case as well
    if (!(status == ZX_OK || status == ZX_ERR_TIMED_OUT)) {
      return ERROR(status);
    }
  }

  nfds_t nfds = 0;
  // |items_index| is the index into the next entry in the |items| array. As not
  // all FDs in the wait set correspond to a kernel wait, the |items_index|
  // value corresponding to a particular FD can be lower than the index of that
  // FD in the |fds| array.
  size_t items_index = 0;
  for (nfds_t i = 0; i < n; ++i) {
    auto& pfd = fds[i];
    auto& io = ios[i];

    if (items_set[i]) {
      uint32_t events;
      io->wait_end(items[items_index].pending, &events);
      pfd.revents = static_cast<int16_t>(events);
      ++items_index;
    }
    // Mask unrequested events. Avoid clearing events that are ignored in pollfd::events.
    pfd.revents &= static_cast<int16_t>(pfd.events | POLLNVAL | POLLHUP | POLLERR);
    if (pfd.revents != 0) {
      ++nfds;
    }
  }

  return nfds;
}

__EXPORT
int poll(struct pollfd* fds, nfds_t n, int timeout) {
  struct timespec timeout_ts = {
      .tv_sec = timeout / 1000,
      .tv_nsec = (timeout % 1000) * 1000000,
  };
  struct timespec* ts = timeout >= 0 ? &timeout_ts : nullptr;
  return ppoll(fds, n, ts, nullptr);
}

__EXPORT
int select(int n, fd_set* __restrict rfds, fd_set* __restrict wfds, fd_set* __restrict efds,
           struct timeval* __restrict tv) {
  if (n > FD_SETSIZE || n < 0) {
    return ERRNO(EINVAL);
  }

  auto timeout = zx::duration::infinite();
  if (tv) {
    if (tv->tv_sec < 0 || tv->tv_usec < 0) {
      return ERRNO(EINVAL);
    }
    timeout = zx::sec(tv->tv_sec) + zx::usec(tv->tv_usec);
  }

  if (n == 0) {
    std::this_thread::sleep_for(std::chrono::nanoseconds(timeout.to_nsecs()));
    return 0;
  }

  // TODO(https://fxbug.dev/71558): investigate VLA alternatives.
  fdio_ptr ios[n];
  zx_wait_item_t items[n];
  size_t nitems = 0;

  for (int fd = 0; fd < n; ++fd) {
    uint32_t events = 0;
    if (rfds && FD_ISSET(fd, rfds))
      events |= POLLIN;
    if (wfds && FD_ISSET(fd, wfds))
      events |= POLLOUT;
    if (efds && FD_ISSET(fd, efds))
      events |= POLLERR;

    auto& io = ios[fd];
    if (events == 0) {
      io = nullptr;
      continue;
    }

    if ((io = fd_to_io(fd)) == nullptr) {
      return ERROR(ZX_ERR_INVALID_ARGS);
    }

    zx_handle_t h;
    zx_signals_t sigs;
    io->wait_begin(events, &h, &sigs);
    if (h == ZX_HANDLE_INVALID) {
      return ERROR(ZX_ERR_INVALID_ARGS);
    }
    items[nitems] = {
        .handle = h,
        .waitfor = sigs,
    };
    ++nitems;
  }

  zx_status_t status = zx::handle::wait_many(items, nitems, zx::deadline_after(timeout));
  // pending signals could be reported on ZX_ERR_TIMED_OUT case as well
  if (!(status == ZX_OK || status == ZX_ERR_TIMED_OUT)) {
    return ERROR(status);
  }

  int nfds = 0;
  size_t j = 0;
  for (int fd = 0; fd < n; fd++) {
    auto io = ios[fd];
    if (io == nullptr) {
      // skip an invalid entry
      continue;
    }
    if (j < nitems) {
      uint32_t events = 0;
      io->wait_end(items[j].pending, &events);
      if (rfds && FD_ISSET(fd, rfds)) {
        if (events & POLLIN) {
          ++nfds;
        } else {
          FD_CLR(fd, rfds);
        }
      }
      if (wfds && FD_ISSET(fd, wfds)) {
        if (events & POLLOUT) {
          ++nfds;
        } else {
          FD_CLR(fd, wfds);
        }
      }
      if (efds && FD_ISSET(fd, efds)) {
        if (events & POLLERR) {
          ++nfds;
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
    ++j;
  }

  return nfds;
}

__EXPORT
int ioctl(int fd, int req, ...) {
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }

  va_list ap;
  va_start(ap, req);
  Errno e = io->posix_ioctl(req, ap);
  va_end(ap);
  if (e.is_error()) {
    return ERRNO(e.e);
  }
  return 0;
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
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  auto& ioflag = io->ioflag();
  // The |flags| are typically used to express intent *not* to issue SIGPIPE
  // via MSG_NOSIGNAL. Applications use this frequently to avoid having to
  // install additional signal handlers to handle cases where connection has
  // been closed by remote end.
  bool nonblocking = (ioflag & IOFLAG_NONBLOCK) || (flags & MSG_DONTWAIT);
  flags &= ~MSG_DONTWAIT;
  zx::time deadline = zx::deadline_after(io->sndtimeo());
  for (;;) {
    size_t actual;
    int16_t out_code;
    zx_status_t status = io->sendmsg(msg, flags, &actual, &out_code);
    if (!nonblocking) {
      switch (status) {
        case ZX_OK:
          if (out_code != EWOULDBLOCK) {
            break;
          }
          __FALLTHROUGH;
        case ZX_ERR_SHOULD_WAIT:
          status = fdio_wait(io, FDIO_EVT_WRITABLE, deadline, nullptr);
          if (status == ZX_OK) {
            continue;
          }
          if (status == ZX_ERR_TIMED_OUT) {
            status = ZX_ERR_SHOULD_WAIT;
          }
      }
    }
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
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  auto& ioflag = io->ioflag();
  bool nonblocking = (ioflag & IOFLAG_NONBLOCK) || (flags & MSG_DONTWAIT);
  flags &= ~MSG_DONTWAIT;
  zx::time deadline = zx::deadline_after(io->rcvtimeo());
  for (;;) {
    size_t actual;
    int16_t out_code;
    zx_status_t status = io->recvmsg(msg, flags, &actual, &out_code);
    if (!nonblocking) {
      switch (status) {
        case ZX_OK:
          if (out_code != EWOULDBLOCK) {
            break;
          }
          __FALLTHROUGH;
        case ZX_ERR_SHOULD_WAIT:
          status = fdio_wait(io, FDIO_EVT_READABLE, deadline, nullptr);
          if (status == ZX_OK) {
            continue;
          }
          if (status == ZX_ERR_TIMED_OUT) {
            status = ZX_ERR_SHOULD_WAIT;
          }
      }
    }
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
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }

  int16_t out_code;
  zx_status_t status = io->shutdown(how, &out_code);
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
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  zx_handle_t handle;
  zx_status_t status = io->borrow_channel(&handle);
  if (status != ZX_OK) {
    return ERROR(status);
  }
  auto directory_admin = fidl::UnownedClientEnd<fio::DirectoryAdmin>(handle);
  if (!directory_admin.is_valid()) {
    return ERRNO(ENOTSUP);
  }
  auto result = fidl::WireCall(directory_admin).QueryFilesystem();
  if (result.status() != ZX_OK) {
    return ERROR(result.status());
  }
  fidl::WireResponse<fio::DirectoryAdmin::QueryFilesystem>* response = result.Unwrap();
  if (response->s != ZX_OK) {
    return ERROR(response->s);
  }
  fio::wire::FilesystemInfo* info = response->info.get();
  if (info == nullptr) {
    return ERRNO(EIO);
  }

  info->name[fio::wire::kMaxFsNameBuffer - 1] = '\0';

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
    vstats.f_fsid = stats.f_fsid.__val[0] + ((static_cast<uint64_t>(stats.f_fsid.__val[1])) << 32);

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
