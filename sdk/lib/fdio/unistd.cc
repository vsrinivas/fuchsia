// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdk/lib/fdio/unistd.h"

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/private.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/vfs.h>
#include <lib/stdcompat/string_view.h>
#include <lib/zx/result.h>
#include <lib/zxio/ops.h>
#include <lib/zxio/posix_mode.h>
#include <lib/zxio/types.h>
#include <poll.h>
#include <sys/file.h>
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
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <cstdarg>
#include <thread>
#include <utility>
#include <variant>

#include <fbl/auto_lock.h>
#include <fbl/string_buffer.h>
#include <safemath/checked_math.h>

#include "sdk/lib/fdio/cleanpath.h"
#include "sdk/lib/fdio/fdio_unistd.h"
#include "sdk/lib/fdio/internal.h"
#include "sdk/lib/fdio/namespace/namespace.h"
#include "sdk/lib/fdio/zxio.h"

namespace fio = fuchsia_io;

static_assert(IOFLAG_CLOEXEC == FD_CLOEXEC, "Unexpected fdio flags value");

// non-thread-safe emulation of unistd io functions using the fdio transports

// Verify the O_* flags which align with fuchsia.io.
static_assert(O_PATH == static_cast<uint32_t>(fio::wire::OpenFlags::kNodeReference),
              "Open Flag mismatch");
static_assert(O_CREAT == static_cast<uint32_t>(fio::wire::OpenFlags::kCreate),
              "Open Flag mismatch");
static_assert(O_EXCL == static_cast<uint32_t>(fio::wire::OpenFlags::kCreateIfAbsent),
              "Open Flag mismatch");
static_assert(O_TRUNC == static_cast<uint32_t>(fio::wire::OpenFlags::kTruncate),
              "Open Flag mismatch");
static_assert(O_DIRECTORY == static_cast<uint32_t>(fio::wire::OpenFlags::kDirectory),
              "Open Flag mismatch");
static_assert(O_APPEND == static_cast<uint32_t>(fio::wire::OpenFlags::kAppend),
              "Open Flag mismatch");

// The mask of "1:1" flags which match between both open flag representations.
constexpr fio::wire::OpenFlags kZxioFsMask =
    fio::wire::OpenFlags::kNodeReference | fio::wire::OpenFlags::kCreate |
    fio::wire::OpenFlags::kCreateIfAbsent | fio::wire::OpenFlags::kTruncate |
    fio::wire::OpenFlags::kDirectory | fio::wire::OpenFlags::kAppend |
    fio::wire::OpenFlags::kCloneSameRights;

constexpr fio::wire::OpenFlags kZxioFsFlags =
    kZxioFsMask | fio::wire::OpenFlags::kPosixWritable | fio::wire::OpenFlags::kPosixExecutable |
    fio::wire::OpenFlags::kNotDirectory | fio::wire::OpenFlags::kCloneSameRights;

// Verify that the remaining O_* flags don't overlap with the ZXIO_FS flags.
static_assert(!(O_RDONLY & static_cast<uint32_t>(kZxioFsFlags)),
              "Unexpected collision with static_cast<uint32_t>(kZxioFsFlags)");
static_assert(!(O_WRONLY & static_cast<uint32_t>(kZxioFsFlags)),
              "Unexpected collision with static_cast<uint32_t>(kZxioFsFlags)");
static_assert(!(O_RDWR & static_cast<uint32_t>(kZxioFsFlags)),
              "Unexpected collision with static_cast<uint32_t>(kZxioFsFlags)");
static_assert(!(O_NONBLOCK & static_cast<uint32_t>(kZxioFsFlags)),
              "Unexpected collision with static_cast<uint32_t>(kZxioFsFlags)");
static_assert(!(O_DSYNC & static_cast<uint32_t>(kZxioFsFlags)),
              "Unexpected collision with static_cast<uint32_t>(kZxioFsFlags)");
static_assert(!(O_SYNC & static_cast<uint32_t>(kZxioFsFlags)),
              "Unexpected collision with static_cast<uint32_t>(kZxioFsFlags)");
static_assert(!(O_RSYNC & static_cast<uint32_t>(kZxioFsFlags)),
              "Unexpected collision with static_cast<uint32_t>(kZxioFsFlags)");
static_assert(!(O_NOFOLLOW & static_cast<uint32_t>(kZxioFsFlags)),
              "Unexpected collision with static_cast<uint32_t>(kZxioFsFlags)");
static_assert(!(O_CLOEXEC & static_cast<uint32_t>(kZxioFsFlags)),
              "Unexpected collision with static_cast<uint32_t>(kZxioFsFlags)");
static_assert(!(O_NOCTTY & static_cast<uint32_t>(kZxioFsFlags)),
              "Unexpected collision with static_cast<uint32_t>(kZxioFsFlags)");
static_assert(!(O_ASYNC & static_cast<uint32_t>(kZxioFsFlags)),
              "Unexpected collision with static_cast<uint32_t>(kZxioFsFlags)");
static_assert(!(O_DIRECT & static_cast<uint32_t>(kZxioFsFlags)),
              "Unexpected collision with static_cast<uint32_t>(kZxioFsFlags)");
static_assert(!(O_LARGEFILE & static_cast<uint32_t>(kZxioFsFlags)),
              "Unexpected collision with static_cast<uint32_t>(kZxioFsFlags)");
static_assert(!(O_NOATIME & static_cast<uint32_t>(kZxioFsFlags)),
              "Unexpected collision with static_cast<uint32_t>(kZxioFsFlags)");
static_assert(!(O_TMPFILE & static_cast<uint32_t>(kZxioFsFlags)),
              "Unexpected collision with static_cast<uint32_t>(kZxioFsFlags)");

static fio::wire::OpenFlags fdio_flags_to_zxio(uint32_t flags) {
  fio::wire::OpenFlags rights = {};
  switch (flags & O_ACCMODE) {
    case O_RDONLY:
      rights |= fio::wire::OpenFlags::kRightReadable;
      break;
    case O_WRONLY:
      rights |= fio::wire::OpenFlags::kRightWritable;
      break;
    case O_RDWR:
      rights |= fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kRightWritable;
      break;
  }

  fio::wire::OpenFlags result = rights | fio::wire::OpenFlags::kDescribe |
                                (static_cast<fio::wire::OpenFlags>(flags) & kZxioFsMask);

  if (!(result & fio::wire::OpenFlags::kNodeReference)) {
    result |= fio::wire::OpenFlags::kPosixWritable | fio::wire::OpenFlags::kPosixExecutable;
  }
  return result;
}

static uint32_t zxio_flags_to_fdio(fio::wire::OpenFlags flags) {
  uint32_t result = 0;
  if ((flags & (fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kRightWritable)) ==
      (fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kRightWritable)) {
    result |= O_RDWR;
  } else if (flags & fio::wire::OpenFlags::kRightWritable) {
    result |= O_WRONLY;
  } else {
    result |= O_RDONLY;
  }

  result |= static_cast<uint32_t>(flags & kZxioFsMask);
  return result;
}

fdio_ptr fdio_iodir(int dirfd, std::string_view& in_out_path) {
  const bool root = cpp20::starts_with(in_out_path, '/');
  if (root) {
    // Since we are sending a request to the root handle, the
    // rest of the in_out_path should be canonicalized as a relative
    // path (relative to this root handle).
    while (cpp20::starts_with(in_out_path, '/')) {
      in_out_path.remove_prefix(1);
      if (in_out_path.empty()) {
        in_out_path = std::string_view(".");
      }
    }
  }
  const fbl::AutoLock lock(&fdio_lock);
  if (root) {
    return fdio_root_handle.get();
  }
  if (dirfd == AT_FDCWD) {
    return fdio_cwd_handle.get();
  }
  return fd_to_io_locked(dirfd);
}

namespace fdio_internal {

zx::result<fdio_ptr> open_at_impl(int dirfd, const char* path, int flags, uint32_t mode,
                                  bool enforce_eisdir) {
  // Emulate EISDIR behavior from
  // http://pubs.opengroup.org/onlinepubs/9699919799/functions/open.html
  const bool flags_incompatible_with_directory =
      ((flags & ~O_PATH & O_ACCMODE) != O_RDONLY) || (flags & O_CREAT);
  const fio::wire::OpenFlags fio_flags = fdio_flags_to_zxio(static_cast<uint32_t>(flags));

  return open_at_impl(dirfd, path, fio_flags, mode,
                      {
                          .disallow_directory = enforce_eisdir && flags_incompatible_with_directory,
                          .allow_absolute_path = true,
                      });
}

zx::result<fdio_ptr> open_at_impl(int dirfd, const char* path, fio::wire::OpenFlags flags,
                                  uint32_t mode, OpenAtOptions options) {
  if (path == nullptr) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (path[0] == '\0') {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  fdio_internal::PathBuffer clean_buffer;
  bool has_ending_slash;
  const bool cleaned = CleanPath(path, &clean_buffer, &has_ending_slash);
  if (!cleaned) {
    return zx::error(ZX_ERR_BAD_PATH);
  }

  std::string_view clean = clean_buffer;

  // Some callers such as the fdio_open_..._at() family do not permit absolute paths.
  if (!options.allow_absolute_path && cpp20::starts_with(clean, '/')) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  const fdio_ptr iodir = fdio_iodir(dirfd, clean);
  if (iodir == nullptr) {
    return zx::error(ZX_ERR_BAD_HANDLE);
  }

  if (options.disallow_directory && has_ending_slash) {
    return zx::error(ZX_ERR_NOT_FILE);
  }

  if (has_ending_slash) {
    flags |= fio::wire::OpenFlags::kDirectory;
  }

  if (!(flags & fio::wire::OpenFlags::kDirectory)) {
    // At this point we're not sure if the path refers to a directory.
    // To emulate EISDIR behavior, if the flags are not compatible with directory,
    // use this flag to instruct open to error if the path turns out to be a directory.
    // Otherwise, opening a directory with O_RDWR will incorrectly succeed.
    if (options.disallow_directory) {
      flags |= fio::wire::OpenFlags::kNotDirectory;
    }
  }
  if (flags & fio::wire::OpenFlags::kNodeReference) {
    flags &= fio::wire::kOpenFlagsAllowedWithNodeReference;
  }
  return iodir->open(clean, flags, mode);
}

// Open |path| from the |dirfd| directory, enforcing the POSIX EISDIR error condition. Specifically,
// ZX_ERR_NOT_FILE will be returned when opening a directory with write access/O_CREAT.
zx::result<fdio_ptr> open_at(int dirfd, const char* path, int flags, uint32_t mode) {
  return open_at_impl(dirfd, path, flags, mode, true);
}

// Open |path| from the |dirfd| directory, but allow creating directories/opening them with
// write access. Note that this differs from POSIX behavior.
zx::result<fdio_ptr> open_at_ignore_eisdir(int dirfd, const char* path, int flags, uint32_t mode) {
  return open_at_impl(dirfd, path, flags, mode, false);
}

// Open |path| from the current working directory, respecting EISDIR.
zx::result<fdio_ptr> open(const char* path, int flags, uint32_t mode) {
  return open_at(AT_FDCWD, path, flags, mode);
}

void update_cwd_path(const char* path) __TA_REQUIRES(fdio_cwd_lock) {
  if (path[0] == '/') {
    // it's "absolute", but we'll still parse it as relative (from /)
    // so that we normalize the path (resolving, ., .., //, etc)
    fdio_cwd_path.Set("/");
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
      char* x = strrchr(fdio_cwd_path.data(), '/');
      if (x == nullptr) {
        // shouldn't ever happen
        goto wat;
      }
      // remove the current trailing path segment from cwd
      if (x == fdio_cwd_path.data()) {
        // but never remove the first /
        fdio_cwd_path[1] = 0;
      } else {
        x[0] = 0;
      }
      continue;
    }
    // regular path segment, append to cwd_path
    const size_t len = fdio_cwd_path.length();
    if ((len + seglen + 2) >= PATH_MAX) {
      // doesn't fit, shouldn't happen, but...
      goto wat;
    }
    if (len != 1) {
      // if len is 1, path is "/", so don't append a '/'
      fdio_cwd_path.Append('/');
    }
    fdio_cwd_path.Append(path, seglen);
  }
  return;

wat:
  fdio_cwd_path.Set("(unknown");
}

// Buffer used to store a single path component and its null terminator.
using NameBuffer = fbl::StringBuffer<NAME_MAX>;

// Opens the directory containing path
//
// Returns the last component of the path in `out`.  If `is_dir_out` is nullptr,
// a trailing slash will be added to the name if the last component happens to
// be a directory.  Otherwise, `is_dir_out` will be set to indicate whether the
// last component is a directory.
zx::result<fdio_ptr> opendir_containing_at(int dirfd, const char* path, NameBuffer* out,
                                           bool* is_dir_out) {
  if (path == nullptr) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  fdio_internal::PathBuffer clean_buffer;
  bool is_dir;
  const bool cleaned = fdio_internal::CleanPath(path, &clean_buffer, &is_dir);
  if (!cleaned) {
    return zx::error(ZX_ERR_BAD_PATH);
  }
  std::string_view clean = clean_buffer;

  const fdio_ptr iodir = fdio_iodir(dirfd, clean);
  if (iodir == nullptr) {
    return zx::error(ZX_ERR_BAD_HANDLE);
  }

  // Split the clean path into everything up to the last slash and the last component.
  std::string_view name = clean;
  std::string_view base;
  auto last_slash = clean.rfind('/');
  if (last_slash != std::string_view::npos) {
    name.remove_prefix(last_slash + 1);
    base = clean.substr(0, last_slash);
  }

  if (name.length() + (is_dir ? 1 : 0) > NAME_MAX) {
    return zx::error(ZX_ERR_BAD_PATH);
  }

  // Copy the trailing 'name' to out.
  out->Append(name);
  if (is_dir_out) {
    *is_dir_out = is_dir;
  } else if (is_dir) {
    // TODO(fxbug.dev/37408): Propagate whether path is directory without using
    // trailing backslash to simplify server-side path parsing.
    // This might require refactoring trailing backslash checks out of
    // lower filesystem layers and associated FIDL APIs.

    out->Append('/');
  }

  if (base.empty() && !cpp20::starts_with(name, '/')) {
    base = ".";
  }

  return iodir->open(base, fdio_flags_to_zxio(O_RDONLY | O_DIRECTORY), 0);
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
    const zx_status_t status = fdio_ns_create(&fdio_root_ns);
    ZX_ASSERT_MSG(status == ZX_OK, "Failed to create root namespace: %s",
                  zx_status_get_string(status));
  }

  fdio_ptr use_for_stdio = nullptr;

  // extract handles we care about
  for (uint32_t n = 0; n < handle_count; n++) {
    const unsigned arg = PA_HND_ARG(handle_info[n]);
    const zx_handle_t h = handle[n];

    // precalculate the fd from |arg|, for FDIO cases to use.
    const unsigned arg_fd = arg & (~FDIO_FLAG_USE_FOR_STDIO);

    switch (PA_HND_TYPE(handle_info[n])) {
      case PA_FD: {
        zx::result io = fdio::create(zx::handle(h));
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

  if (use_for_stdio == nullptr) {
    zx::result null = fdio_internal::zxio::create_null();
    ZX_ASSERT_MSG(null.is_ok(), "%s", null.status_string());
    use_for_stdio = std::move(null.value());
  }

  // configure stdin/out/err if not init'd
  for (uint32_t n = 0; n < 3; n++) {
    fdio_fdtab[n].try_set(use_for_stdio);
  }

  fdio_ptr default_io = nullptr;
  auto get_default = [&default_io]() {
    if (default_io == nullptr) {
      zx::result default_result = fdio_internal::zxio::create();
      ZX_ASSERT_MSG(default_result.is_ok(), "%s", default_result.status_string());
      default_io = std::move(default_result.value());
    }
    return default_io;
  };

  zx::result root = fdio_ns_open_root(fdio_root_ns);
  if (root.is_ok()) {
    ZX_ASSERT(fdio_root_handle.try_set(root.value()));
    zx::result cwd = fdio_internal::open(fdio_cwd_path.c_str(), O_RDONLY | O_DIRECTORY, 0);
    if (cwd.is_ok()) {
      ZX_ASSERT(fdio_cwd_handle.try_set(cwd.value()));
    } else {
      ZX_ASSERT(fdio_cwd_handle.try_set(get_default()));
    }
  } else {
    ZX_ASSERT(fdio_root_handle.try_set(get_default()));
    ZX_ASSERT(fdio_cwd_handle.try_set(get_default()));
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
    __UNUSED const fdio_ptr io = var.release();
  }
}

__EXPORT
zx_status_t fdio_ns_get_installed(fdio_ns_t** ns) {
  zx_status_t status = ZX_OK;
  const fbl::AutoLock lock(&fdio_lock);
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
  const zx_status_t status = zx_object_wait_one(h, signals, deadline.get(), &pending);
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
  const zx_status_t status = io->get_attr(&attr);
  if (status != ZX_OK) {
    return status;
  }

  memset(s, 0, sizeof(struct stat));
  s->st_mode = zxio_get_posix_mode(attr.protocols, attr.abilities);
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
extern "C" __EXPORT zx_status_t _mmap_get_vmo_from_fd(int mmap_prot, int mmap_flags, int fd,
                                                      zx_handle_t* out_vmo) {
  assert(out_vmo != nullptr);
  const fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ZX_ERR_BAD_HANDLE;
  }

  // Convert mmap flags into respective ZXIO flags.
  zxio_vmo_flags_t zxio_flags = 0;

  // Handle protection bits and mode flags.
  zxio_flags |= (mmap_prot & PROT_READ) ? ZXIO_VMO_READ : 0;
  zxio_flags |= (mmap_prot & PROT_WRITE) ? ZXIO_VMO_WRITE : 0;
  zxio_flags |= (mmap_prot & PROT_EXEC) ? ZXIO_VMO_EXECUTE : 0;
  zxio_flags |= (mmap_flags & MAP_PRIVATE) ? ZXIO_VMO_PRIVATE_CLONE : 0;
  // We cannot specify ZXIO_VMO_SHARED_BUFFER as not all filesystems support shared mappings.
  // This does not affect behavior of filesystems that do not support writable shared mappings.
  // Filesystems which support PROT_WRITE + MAP_SHARED can enable the `supports_mmap_shared_write`
  // option in the fs_test suite to validate this case.

  return zxio_vmo_get(&io->zxio_storage().io, zxio_flags, out_vmo);
}

__EXPORT
int unlinkat(int dirfd, const char* path, int flags) {
  fdio_internal::NameBuffer name;
  bool is_dir;
  zx::result io = fdio_internal::opendir_containing_at(dirfd, path, &name, &is_dir);
  if (io.is_error()) {
    return ERROR(io.status_value());
  }
  if (is_dir) {
    flags |= AT_REMOVEDIR;
  }
  return STATUS(io->unlink(name, flags));
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
  zx_off_t zx_offset;
  if (!safemath::MakeCheckedNum(offset).AssignIfValid(&zx_offset)) {
    return ERRNO(EINVAL);
  }
  const fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  const bool blocking = (io->ioflag() & IOFLAG_NONBLOCK) == 0;
  const zx::time deadline = zx::deadline_after(io->rcvtimeo());

  zx_iovec_t zx_iov[iovcnt];
  for (int i = 0; i < iovcnt; ++i) {
    zx_iov[i] = {
        .buffer = iov[i].iov_base,
        .capacity = iov[i].iov_len,
    };
  }

  for (;;) {
    size_t actual;
    zx_status_t status =
        zxio_readv_at(&io->zxio_storage().io, zx_offset, zx_iov, iovcnt, 0, &actual);
    if (status == ZX_ERR_SHOULD_WAIT && blocking) {
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
  zx_off_t zx_offset;
  if (!safemath::MakeCheckedNum(offset).AssignIfValid(&zx_offset)) {
    return ERRNO(EINVAL);
  }
  const fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  const bool blocking = (io->ioflag() & IOFLAG_NONBLOCK) == 0;
  const zx::time deadline = zx::deadline_after(io->sndtimeo());

  zx_iovec_t zx_iov[iovcnt];
  for (int i = 0; i < iovcnt; ++i) {
    zx_iov[i] = {
        .buffer = iov[i].iov_base,
        .capacity = iov[i].iov_len,
    };
  }

  for (;;) {
    size_t actual;
    zx_status_t status =
        zxio_writev_at(&io->zxio_storage().io, zx_offset, zx_iov, iovcnt, 0, &actual);
    if (status == ZX_ERR_SHOULD_WAIT && blocking) {
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
  std::variant reference = GetLastReference(std::move(io));
  auto* ptr = std::get_if<fdio::last_reference>(&reference);
  if (ptr) {
    return STATUS(ptr->Close());
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
    const fbl::AutoLock lock(&fdio_lock);
    const fdio_ptr io = fd_to_io_locked(oldfd);
    if (io == nullptr) {
      return ERRNO(EBADF);
    }
    io_to_close = fdio_fdtab[newfd].replace(io);
  }
  return newfd;
}

__EXPORT
int dup(int oldfd) {
  const fbl::AutoLock lock(&fdio_lock);
  const fdio_ptr io = fd_to_io_locked(oldfd);
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
#define GET_INT_ARG(ARG)              \
  va_list args;                       \
  va_start(args, cmd);                \
  const int(ARG) = va_arg(args, int); \
  va_end(args)

  switch (cmd) {
    case F_DUPFD:
    case F_DUPFD_CLOEXEC: {
      // TODO(fxbug.dev/30920) Implement CLOEXEC.
      GET_INT_ARG(starting_fd);
      if (starting_fd < 0) {
        return ERRNO(EINVAL);
      }
      const fbl::AutoLock lock(&fdio_lock);
      const fdio_ptr io = fd_to_io_locked(fd);
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
      const fdio_ptr io = fd_to_io(fd);
      if (io == nullptr) {
        return ERRNO(EBADF);
      }
      const int flags = static_cast<int>(io->ioflag() & IOFLAG_FD_FLAGS);
      // POSIX mandates that the return value be nonnegative if successful.
      assert(flags >= 0);
      return flags;
    }
    case F_SETFD: {
      const fdio_ptr io = fd_to_io(fd);
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
      const fdio_ptr io = fd_to_io(fd);
      if (io == nullptr) {
        return ERRNO(EBADF);
      }
      fio::wire::OpenFlags flags;
      zx_status_t status = io->get_flags(&flags);
      if (status == ZX_ERR_NOT_SUPPORTED) {
        // We treat this as non-fatal, as it's valid for a remote to
        // simply not support FCNTL, but we still want to correctly
        // report the state of the (local) NONBLOCK flag
        flags = {};
        status = ZX_OK;
      }
      uint32_t fdio_flags = zxio_flags_to_fdio(flags);
      if (io->ioflag() & IOFLAG_NONBLOCK) {
        fdio_flags |= O_NONBLOCK;
      }
      if (status != ZX_OK) {
        return ERROR(status);
      }
      return fdio_flags;
    }
    case F_SETFL: {
      const fdio_ptr io = fd_to_io(fd);
      if (io == nullptr) {
        return ERRNO(EBADF);
      }
      GET_INT_ARG(fdio_flags);

      const fio::wire::OpenFlags flags = fdio_flags_to_zxio(fdio_flags & ~O_NONBLOCK);
      zx_status_t status = io->set_flags(flags);

      // Some remotes don't support setting flags; we
      // can adjust their local flags anyway if NONBLOCK
      // is the only bit being toggled.
      if (status == ZX_ERR_NOT_SUPPORTED && ((fdio_flags | O_NONBLOCK) == O_NONBLOCK)) {
        status = ZX_OK;
      }

      if (status != ZX_OK) {
        return ERROR(status);
      }
      if (fdio_flags & O_NONBLOCK) {
        io->ioflag() |= IOFLAG_NONBLOCK;
      } else {
        io->ioflag() &= ~IOFLAG_NONBLOCK;
      }
      return 0;
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
int flock(int fd, int operation) {
  zxio_advisory_lock_req_t lock_req;
  lock_req.wait = true;
  if (operation & LOCK_NB) {
    lock_req.wait = false;
    operation &= ~LOCK_NB;
  }
  switch (operation) {
    case LOCK_SH:
      lock_req.type = ADVISORY_LOCK_SHARED;
      break;
    case LOCK_EX:
      lock_req.type = ADVISORY_LOCK_EXCLUSIVE;
      break;
    case LOCK_UN:
      lock_req.type = ADVISORY_LOCK_UNLOCK;
      break;
    default: {
      return ERRNO(EINVAL);
    }
  }

  fdio_t* fdio = fdio_unsafe_fd_to_io(fd);
  if (fdio == nullptr) {
    return ERRNO(EBADF);
  }
  zxio_t* io = fdio_get_zxio(fdio);
  const zx_status_t status = zxio_get_ops(io)->advisory_lock(io, &lock_req);

  fdio_unsafe_release(fdio);
  return STATUS(status);
}

__EXPORT
off_t lseek(int fd, off_t offset, int whence) {
  const fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }

  static_assert(SEEK_SET == ZXIO_SEEK_ORIGIN_START);
  static_assert(SEEK_CUR == ZXIO_SEEK_ORIGIN_CURRENT);
  static_assert(SEEK_END == ZXIO_SEEK_ORIGIN_END);

  size_t result = 0u;
  const zx_status_t status = zxio_seek(&io->zxio_storage().io, whence, offset, &result);
  if (status == ZX_ERR_WRONG_TYPE) {
    // Although 'ESPIPE' is a bit of a misnomer, it is the valid errno
    // for any fd which does not implement seeking (i.e., for pipes,
    // sockets, etc).
    return ERRNO(ESPIPE);
  }
  return status != ZX_OK ? ERROR(status) : static_cast<off_t>(result);
}

static int truncateat(int dirfd, const char* path, off_t len) {
  zx::result io = fdio_internal::open_at(dirfd, path, O_WRONLY, 0);
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
  const fdio_ptr io = fd_to_io(fd);
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
  fdio_internal::NameBuffer oldname;
  zx::result io_oldparent =
      fdio_internal::opendir_containing_at(olddirfd, oldpath, &oldname, nullptr);
  if (io_oldparent.is_error()) {
    return ERROR(io_oldparent.status_value());
  }

  fdio_internal::NameBuffer newname;
  zx::result io_newparent =
      fdio_internal::opendir_containing_at(newdirfd, newpath, &newname, nullptr);
  if (io_newparent.is_error()) {
    return ERROR(io_newparent.status_value());
  }

  zx_handle_t token;
  const zx_status_t status = io_newparent->get_token(&token);
  if (status != ZX_OK) {
    return ERROR(status);
  }
  return STATUS((io_oldparent.value().get()->*op_getter)(oldname, token, newname));
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
int linkat(int olddirfd, const char* oldpath, int newdirfd, const char* newpath, int flags) {
  // Accept AT_SYMLINK_FOLLOW, but ignore it because Fuchsia does not support symlinks yet.
  if (flags & ~AT_SYMLINK_FOLLOW) {
    return ERRNO(EINVAL);
  }

  return two_path_op_at(olddirfd, oldpath, newdirfd, newpath, &fdio_t::link);
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
  zx::result io = fdio_internal::open_at(dirfd, path, flags, mode);
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
  const int ret = vopenat(AT_FDCWD, path, flags, ap);
  va_end(ap);
  return ret;
}

__EXPORT
int openat(int dirfd, const char* path, int flags, ...) {
  va_list ap;
  va_start(ap, flags);
  const int ret = vopenat(dirfd, path, flags, ap);
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
  const fdio_ptr io = fd_to_io(fd);
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
  const fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  return STATUS(fdio_stat(io, s));
}

int fstatat(int dirfd, std::string_view filename, struct stat* s, int flags) {
  zx::result io = fdio_internal::open_at(dirfd, filename.data(), O_PATH, 0);
  if (io.is_error()) {
    return ERROR(io.status_value());
  }
  return STATUS(fdio_stat(io.value(), s));
}

__EXPORT
int fstatat(int dirfd, const char* fn, struct stat* s, int flags) {
  return fstatat(dirfd, std::string_view(fn), s, flags);
}

__EXPORT
int stat(const char* fn, struct stat* s) { return fstatat(AT_FDCWD, fn, s, 0); }

__EXPORT
int lstat(const char* path, struct stat* buf) { return stat(path, buf); }

static constexpr char kUnreachable[] = "(unreachable)";

__EXPORT
char* realpath(const char* __restrict filename, char* __restrict resolved) {
  const std::string_view filename_view(filename);

  bool do_stat = true;
  fdio_internal::PathBuffer abspath_buffer;
  if (!cpp20::starts_with(filename_view, '/')) {
    // Convert 'filename' from a relative path to an absolute path.
    {
      const fbl::AutoLock cwd_lock(&fdio_cwd_lock);
      if (fdio_cwd_path.length() + 1 + filename_view.length() >= PATH_MAX) {
        errno = ENAMETOOLONG;
        return nullptr;
      }
      if (std::string_view{fdio_cwd_path} == kUnreachable) {
        do_stat = false;
      }
      abspath_buffer.Append(fdio_cwd_path);
    }
    abspath_buffer.Append('/');
    abspath_buffer.Append(filename);
    filename = abspath_buffer.c_str();
  }
  fdio_internal::PathBuffer clean_buffer;
  {
    bool is_dir;
    const bool cleaned = fdio_internal::CleanPath(filename, &clean_buffer, &is_dir);
    if (!cleaned) {
      errno = EINVAL;
      return nullptr;
    }
  }
  if (do_stat) {
    struct stat s;
    const int ret = fstatat(AT_FDCWD, clean_buffer, &s, 0);
    if (ret < 0) {
      return nullptr;
    }
  }
  return resolved ? strcpy(resolved, clean_buffer.c_str()) : strdup(clean_buffer.c_str());
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
  zx::result io = fdio_internal::open_at_ignore_eisdir(dirfd, path, O_WRONLY, 0);
  if (io.is_error()) {
    return ERROR(io.status_value());
  }
  return STATUS(zx_utimens(io.value(), times, 0));
}

__EXPORT
int futimens(int fd, const struct timespec times[2]) {
  const fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  return STATUS(zx_utimens(io, times, 0));
}

static int socketpair_create(int fd[2], uint32_t options, int flags) {
  constexpr int allowed_flags = O_NONBLOCK | O_CLOEXEC;
  if (flags & ~allowed_flags) {
    return ERRNO(EINVAL);
  }

  zx::result pair = fdio_internal::pipe::create_pair(options);
  if (pair.is_error()) {
    return ERROR(pair.status_value());
  }
  auto [left, right] = pair.value();
  std::array<fdio_ptr, 2> ios = {left, right};

  if (flags & O_NONBLOCK) {
    left->ioflag() |= IOFLAG_NONBLOCK;
    right->ioflag() |= IOFLAG_NONBLOCK;
  }

  if (flags & O_CLOEXEC) {
    left->ioflag() |= IOFLAG_CLOEXEC;
    right->ioflag() |= IOFLAG_CLOEXEC;
  }

  size_t n = 0;

  const fbl::AutoLock lock(&fdio_lock);
  for (int i = 0; i < static_cast<int>(fdio_fdtab.size()); ++i) {
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
int pipe2(int pipefd[2], int flags) { return socketpair_create(pipefd, 0, flags); }

__EXPORT
int pipe(int pipefd[2]) { return pipe2(pipefd, 0); }

__EXPORT
int socketpair(int domain, int type, int protocol, int fd[2]) {
  uint32_t options = 0;

  // Ignore SOCK_CLOEXEC.
  type = type & ~SOCK_CLOEXEC;

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

  return socketpair_create(fd, options, 0);
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
    zx::result io = fdio_internal::open_at(dirfd, filename, O_PATH, 0);
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
  fdio_internal::PathBuffer tmp;
  if (buf == nullptr) {
    buf = tmp.data();
    size = PATH_MAX;
  } else if (size == 0) {
    errno = EINVAL;
    return nullptr;
  }

  char* out = nullptr;
  {
    const fbl::AutoLock lock(&fdio_cwd_lock);
    const size_t len = fdio_cwd_path.length() + 1;
    if (len < size) {
      memcpy(buf, fdio_cwd_path.data(), len);
      out = buf;
    } else {
      errno = ERANGE;
    }
  }

  if (out == tmp.data()) {
    out = strdup(tmp.c_str());
  }
  return out;
}

void fdio_chdir(fdio_ptr io, const char* path) {
  const fbl::AutoLock cwd_lock(&fdio_cwd_lock);
  fdio_internal::update_cwd_path(path);
  const fbl::AutoLock lock(&fdio_lock);
  fdio_cwd_handle.replace(std::move(io));
}

__EXPORT
int chdir(const char* path) {
  zx::result io = fdio_internal::open(path, O_RDONLY | O_DIRECTORY, 0);
  if (io.is_error()) {
    return ERROR(io.status_value());
  }
  fdio_chdir(io.value(), path);
  return 0;
}

static bool resolve_path(const char* relative, fdio_internal::PathBuffer* out_resolved) {
  bool is_dir = false;
  if (relative[0] == '/') {
    return fdio_internal::CleanPath(relative, out_resolved, &is_dir);
  }

  fdio_internal::PathBuffer buffer;
  {
    const fbl::AutoLock cwd_lock(&fdio_cwd_lock);
    buffer.Append(fdio_cwd_path);
  }
  const size_t cwd_length = buffer.length();
  const size_t relative_length = strlen(relative);

  if (cwd_length + relative_length + 2 > PATH_MAX) {
    return false;
  }

  buffer.Append('/');
  buffer.Append(relative, relative_length);
  return fdio_internal::CleanPath(buffer.c_str(), out_resolved, &is_dir);
}

__EXPORT
int chroot(const char* path) {
  fdio_internal::PathBuffer root_path;
  const bool resolved = resolve_path(path, &root_path);
  if (!resolved) {
    return ERRNO(ENAMETOOLONG);
  }

  zx::result io = fdio_internal::open(root_path.c_str(), O_RDONLY | O_DIRECTORY, 0);
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
    const fbl::AutoLock cwd_lock(&fdio_cwd_lock);
    const fbl::AutoLock lock(&fdio_lock);

    const zx_status_t status = fdio_ns_set_root(fdio_root_ns, io.value().get());
    if (status != ZX_OK) {
      return ERROR(status);
    }
    old_root = fdio_root_handle.replace(io.value());

    // We are now committed to the root.

    // If the new root path is a prefix of the cwd path, then we can express the current cwd as a
    // path in the new root by trimming off the prefix. Otherwise, we no longer have a name for the
    // cwd.
    if (root_path.length() > 1) {
      const std::string_view cwd_view(fdio_cwd_path);
      if (cwd_view.find(root_path) == 0u && fdio_cwd_path[root_path.length()] == '/') {
        fdio_cwd_path.RemovePrefix(root_path.length());
      } else {
        fdio_cwd_path.Set(kUnreachable);
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
  // This is only allocated during an iteration.
  std::unique_ptr<zxio_dirent_iterator_t> iterator;

  // A single directory entry returned to user; updated by |readdir|.
  struct dirent de = {};
};

static DIR* internal_opendir(int fd) {
  DIR* dir = new __dirstream;
  mtx_init(&dir->lock, mtx_plain);
  dir->fd = fd;
  return dir;
}

__EXPORT
DIR* opendir(const char* name) {
  const int fd = open(name, O_RDONLY | O_DIRECTORY);
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
  const fdio_ptr io = fd_to_io(fd);
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
  if (dir->iterator) {
    const fdio_ptr io = fd_to_io(dir->fd);
    io->dirent_iterator_destroy(dir->iterator.get());
    dir->iterator.reset();
  }
  close(dir->fd);
  delete dir;
  return 0;
}

__EXPORT
struct dirent* readdir(DIR* dir) {
  const fbl::AutoLock lock(&dir->lock);
  struct dirent* de = &dir->de;

  const fdio_ptr io = fd_to_io(dir->fd);

  // Lazy initialize the iterator.
  if (!dir->iterator) {
    dir->iterator = std::make_unique<zxio_dirent_iterator_t>();
    const zx_status_t status =
        io->dirent_iterator_init(dir->iterator.get(), &io->zxio_storage().io);
    if (status != ZX_OK) {
      errno = fdio_status_to_errno(status);
      return nullptr;
    }
  }
  // We need space for the maximum possible filename plus a null terminator.
  static_assert(sizeof(de->d_name) >= ZXIO_MAX_FILENAME + 1);
  zxio_dirent_t entry = {.name = de->d_name};
  const zx_status_t status = io->dirent_iterator_next(dir->iterator.get(), &entry);
  if (status == ZX_ERR_NOT_FOUND) {
    // Reached the end.
    return nullptr;
  }
  if (status != ZX_OK) {
    errno = fdio_status_to_errno(status);
    return nullptr;
  }
  // zxio doesn't null terminate this string, so we do.
  de->d_name[entry.name_length] = '\0';
  de->d_ino = entry.has.id ? entry.id : fio::wire::kInoUnknown;
  de->d_off = 0;
  // The d_reclen field is nonstandard, but existing code
  // may expect it to be useful as an upper bound on the
  // length of the name.
  de->d_reclen = static_cast<uint16_t>(offsetof(struct dirent, d_name) + entry.name_length + 1);
  if (entry.has.protocols) {
    de->d_type = ([](zxio_node_protocols_t protocols) -> unsigned char {
      if (protocols & ZXIO_NODE_PROTOCOL_DIRECTORY) {
        return DT_DIR;
      }
      if (protocols & ZXIO_NODE_PROTOCOL_FILE) {
        return DT_REG;
      }
      if (protocols & ZXIO_NODE_PROTOCOL_CONNECTOR) {
        // There is no good analogue for FIDL services in POSIX land.
        return DT_UNKNOWN;
      }
      return DT_UNKNOWN;
    })(entry.protocols);
  } else {
    de->d_type = DT_UNKNOWN;
  }
  return de;
}

__EXPORT
void rewinddir(DIR* dir) {
  const fbl::AutoLock lock(&dir->lock);
  if (dir->iterator) {
    const fdio_ptr io = fd_to_io(dir->fd);
    io->dirent_iterator_destroy(dir->iterator.get());
    dir->iterator.reset();
  }
}

__EXPORT
int dirfd(DIR* dir) { return dir->fd; }

__EXPORT
int isatty(int fd) {
  const fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    errno = EBADF;
    return 0;
  }

  bool tty;
  const zx_status_t status = zxio_isatty(&io->zxio_storage().io, &tty);
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
  const fbl::AutoLock lock(&fdio_lock);
  oldmask = __fdio_global_state.umask;
  __fdio_global_state.umask = mask & 0777;
  return oldmask;
}

// TODO: getrlimit(RLIMIT_NOFILE, ...)
constexpr size_t kMaxPollNfds = 1024;

__EXPORT
int ppoll(struct pollfd* fds, nfds_t n, const struct timespec* timeout_ts,
          const sigset_t* sigmask) {
  if (sigmask) {
    return ERRNO(ENOSYS);
  }
  if (n > kMaxPollNfds || n < 0) {
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
    io = fd_to_io(pfd.fd);
    if (io == nullptr) {
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
    const zx_status_t status =
        zx::handle::wait_many(items, static_cast<uint32_t>(nitems), zx::deadline_after(timeout));
    // pending signals could be reported on ZX_ERR_TIMED_OUT case as well
    if (status != ZX_OK && status != ZX_ERR_TIMED_OUT) {
      return ERROR(status);
    }
  }

  int nfds = 0;
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

    io = fd_to_io(fd);
    if (io == nullptr) {
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

  const zx_status_t status =
      zx::handle::wait_many(items, static_cast<uint32_t>(nitems), zx::deadline_after(timeout));
  // pending signals could be reported on ZX_ERR_TIMED_OUT case as well
  if (status != ZX_OK && status != ZX_ERR_TIMED_OUT) {
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
  const fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }

  va_list ap;
  va_start(ap, req);
  const Errno e = io->posix_ioctl(req, ap);
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

  const ssize_t n = recvmsg(fd, &msg, flags);
  if (addrlen != nullptr) {
    *addrlen = msg.msg_namelen;
  }
  return n;
}

__EXPORT
ssize_t sendmsg(int fd, const struct msghdr* msg, int flags) {
  const fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  auto& ioflag = io->ioflag();
  // The |flags| are typically used to express intent *not* to issue SIGPIPE
  // via MSG_NOSIGNAL. Applications use this frequently to avoid having to
  // install additional signal handlers to handle cases where connection has
  // been closed by remote end. Signals aren't a notion on Fuchsia, so this
  // flag can be safely ignored.
  flags &= ~MSG_NOSIGNAL;
  const bool blocking = ((ioflag & IOFLAG_NONBLOCK) | (flags & MSG_DONTWAIT)) == 0;
  flags &= ~MSG_DONTWAIT;
  const zx::time deadline = zx::deadline_after(io->sndtimeo());
  for (;;) {
    size_t actual;
    int16_t out_code;
    zx_status_t status = io->sendmsg(msg, flags, &actual, &out_code);
    if (blocking) {
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
  const fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  auto& ioflag = io->ioflag();
  const bool blocking = ((ioflag & IOFLAG_NONBLOCK) | (flags & MSG_DONTWAIT)) == 0;
  flags &= ~MSG_DONTWAIT;
  const zx::time deadline = zx::deadline_after(io->rcvtimeo());
  for (;;) {
    size_t actual;
    int16_t out_code;
    zx_status_t status = io->recvmsg(msg, flags, &actual, &out_code);
    if (blocking) {
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
  const fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }

  zxio_shutdown_options_t options;
  switch (how) {
    case SHUT_RD:
      options = ZXIO_SHUTDOWN_OPTIONS_READ;
      break;
    case SHUT_WR:
      options = ZXIO_SHUTDOWN_OPTIONS_WRITE;
      break;
    case SHUT_RDWR:
      options = ZXIO_SHUTDOWN_OPTIONS_READ | ZXIO_SHUTDOWN_OPTIONS_WRITE;
      break;
    default:
      return ERRNO(EINVAL);
  }

  int16_t out_code;
  const zx_status_t status = zxio_shutdown(&io->zxio_storage().io, options, &out_code);
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
// computed from the fuchsia_io::FilesystemInfo, so this takes a struct statfs.
static int fs_stat(int fd, struct statfs* buf) {
  const fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  zx_handle_t handle;
  const zx_status_t status = io->borrow_channel(&handle);
  if (status != ZX_OK) {
    return ERROR(status);
  }
  auto directory = fidl::UnownedClientEnd<fuchsia_io::Directory>(handle);
  if (!directory.is_valid()) {
    return ERRNO(ENOTSUP);
  }
  auto result = fidl::WireCall(directory)->QueryFilesystem();
  if (result.status() != ZX_OK) {
    return ERROR(result.status());
  }
  fidl::WireResponse<fuchsia_io::Directory::QueryFilesystem>* response = result.Unwrap();
  if (response->s != ZX_OK) {
    return ERROR(response->s);
  }
  fuchsia_io::wire::FilesystemInfo* info = response->info.get();
  if (info == nullptr) {
    return ERRNO(EIO);
  }

  info->name[fuchsia_io::wire::kMaxFsNameBuffer - 1] = '\0';

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
  const int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return fd;
  }
  const int rv = fstatfs(fd, buf);
  close(fd);
  return rv;
}

__EXPORT
int fstatvfs(int fd, struct statvfs* buf) {
  struct statfs stats = {};
  const int result = fs_stat(fd, &stats);
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
  const int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return fd;
  }
  const int rv = fstatvfs(fd, buf);
  close(fd);
  return rv;
}

// extern "C" is required here, since the corresponding declaration is in an internal musl header:
// zircon/third_party/ulib/musl/src/internal/libc.h
extern "C" __EXPORT int _fd_open_max(void) { return FDIO_MAX_FD; }
