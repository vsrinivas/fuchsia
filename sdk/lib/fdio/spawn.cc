// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.process/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl/txn_header.h>
#include <lib/fit/defer.h>
#include <lib/stdcompat/string_view.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/dlfcn.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/utc.h>

#include <algorithm>
#include <bitset>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <list>
#include <string>
#include <utility>
#include <vector>

#include <fbl/unique_fd.h>

namespace fio = fuchsia_io;
namespace fprocess = fuchsia_process;

constexpr std::string_view kResolvePrefix = "#!resolve ";

// It is possible to setup an infinite loop of interpreters. We want to avoid this being a common
// abuse vector, but also stay out of the way of any complex user setups.
constexpr size_t kMaxInterpreterDepth = 255;

// Maximum allowed length of a #! shebang directive.
// This applies to both types of #! directives - both the '#!resolve' special case and the general
// '#!' case with an arbitrary interpreter - but we use the fuchsia.process/Resolver limit rather
// than define a separate arbitrary limit.
constexpr size_t kMaxInterpreterLineLen =
    kResolvePrefix.size() + fprocess::wire::kMaxResolveNameSize;
static_assert(kMaxInterpreterLineLen < ZX_MIN_PAGE_SIZE,
              "max #! interpreter line length must be less than smallest page size");

// The fdio_spawn_action_t is replicated in various ffi interfaces, including
// the rust and golang standard libraries.
static_assert(sizeof(fdio_spawn_action_t) == 24, "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, action) == 0,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, fd) == 8, "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, fd.local_fd) == 8,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, fd.target_fd) == 12,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, ns) == 8, "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, ns.prefix) == 8,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, ns.handle) == 16,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, h) == 8, "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, h.id) == 8,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, h.handle) == 12,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, name) == 8,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, name.data) == 8,
              "fdio_spawn_action_t must have a stable ABI");

namespace {

void report_error(char* err_msg, const char* format, ...) {
  if (!err_msg)
    return;
  va_list args;
  va_start(args, format);
  vsnprintf(err_msg, FDIO_SPAWN_ERR_MSG_MAX_LENGTH, format, args);
  va_end(args);
}

zx_status_t load_path(const char* path, zx::vmo* out_vmo, char* err_msg) {
  fbl::unique_fd fd;
  zx_status_t status = fdio_open_fd(path,
                                    static_cast<uint32_t>(fio::wire::OpenFlags::kRightReadable |
                                                          fio::wire::OpenFlags::kRightExecutable),
                                    fd.reset_and_get_address());
  if (status != ZX_OK) {
    report_error(err_msg, "Could not open file");
    return status;
  }

  zx::vmo vmo;
  status = fdio_get_vmo_exec(fd.get(), vmo.reset_and_get_address());
  if (status != ZX_OK) {
    report_error(err_msg, "Could not clone VMO for file");
    return status;
  }

  if (strlen(path) >= ZX_MAX_NAME_LEN) {
    const char* p = strrchr(path, '/');
    if (p != nullptr) {
      path = p + 1;
    }
  }

  status = vmo.set_property(ZX_PROP_NAME, path, strlen(path));
  if (status != ZX_OK) {
    report_error(err_msg, "Could not associate pathname with VMO");
    return status;
  }

  *out_vmo = std::move(vmo);
  return status;
}

// resolve_name makes a call to the fuchsia.process.Resolver service and may return a vmo and
// associated loader service, if the name resolves within the current realm.
zx_status_t resolve_name(const char* name, size_t name_len, zx::vmo* out_executable,
                         zx::channel* out_ldsvc, char* err_msg) {
  zx::result endpoints = fidl::CreateEndpoints<fprocess::Resolver>();
  if (!endpoints.is_ok()) {
    report_error(err_msg, "failed to create channel for resolver service: %d",
                 endpoints.status_value());
    return ZX_ERR_INTERNAL;
  }

  fidl::WireSyncClient resolver{std::move(endpoints->client)};
  zx_status_t status =
      fdio_service_connect_by_name(fidl::DiscoverableProtocolName<fprocess::Resolver>,
                                   endpoints->server.TakeChannel().release());
  if (status != ZX_OK) {
    report_error(err_msg, "failed to connect to resolver service: %d (%s)", status,
                 zx_status_get_string(status));
    return ZX_ERR_INTERNAL;
  }

  auto response = resolver->Resolve(fidl::StringView::FromExternal(name, name_len));

  status = response.status();
  if (status != ZX_OK) {
    report_error(err_msg, "failed to send resolver request: %d (%s)", status,
                 zx_status_get_string(status));
    return ZX_ERR_INTERNAL;
  }

  status = response.value().status;
  if (status != ZX_OK) {
    report_error(err_msg, "failed to resolve %.*s: %d (%s)", name_len, name, status,
                 zx_status_get_string(status));
    return status;
  }

  *out_executable = std::move(response.value().executable);
  *out_ldsvc = std::move(response.value().ldsvc.channel());

  return ZX_OK;
}

// Find the starting point of the interpreter and the interpreter arguments in a #! script header.
// Note that the input buffer (line) will be modified to add a NUL after the interpreter name.
zx_status_t parse_interp_spec(char* line, char** interp_start, char** args_start) {
  *args_start = nullptr;

  // Skip the '#!' prefix
  char* next_char = line + 2;

  // Skip whitespace
  next_char += strspn(next_char, " \t");

  // No interpreter specified
  if (*next_char == '\0')
    return ZX_ERR_INVALID_ARGS;

  *interp_start = next_char;

  // Skip the interpreter name
  next_char += strcspn(next_char, " \t");

  if (*next_char == '\0')
    return ZX_OK;

  // Add a NUL after the interpreter name
  *next_char++ = '\0';

  // Look for the args
  next_char += strspn(next_char, " \t");

  if (*next_char == '\0')
    return ZX_OK;

  *args_start = next_char;
  return ZX_OK;
}

// handle_interpreters checks whether the provided vmo starts with a '#!' directive, and handles
// appropriately if it does.
//
// If a '#!' directive is present, we check whether it is either:
//   1) a specific '#!resolve' directive, in which case resolve_name is used to resolve the given
//      executable name into a new executable vmo and appropriate loader service through the
//      fuchsia.process.Resolver service, or
//   2) a general '#!' shebang interpreter directive, in which case the given interpreter is loaded
//      via the current loader service and executable is updated. extra_args will also be appended
//      to, and these arguments should be added to the front of argv.
//
// Directives will be resolved until none are detected, an error is encountered, or a resolution
// limit is reached. Also, mixing the two types is unsupported.
//
// The executable and ldsvc paramters are both inputs to and outputs from this function, and are
// updated based on the resolved directives. executable must always be valid, and ldsvc must be
// valid at minimum for the 2nd case above, though it should generally always be valid as well when
// calling this.
zx_status_t handle_interpreters(zx::vmo* executable, zx::channel* ldsvc,
                                std::list<std::string>* extra_args, char* err_msg) {
  extra_args->clear();

  // Mixing #!resolve and general #! within a single spawn is unsupported so that the #!
  // interpreters can simply be loaded from the current namespace.
  bool handled_resolve = false;
  bool handled_shebang = false;
  for (size_t depth = 0; true; ++depth) {
    // VMO sizes are page aligned and MAX_INTERPRETER_LINE_LEN < ZX_MIN_PAGE_SIZE (asserted above),
    // so there's no use in checking VMO size explicitly here. Either the read fails because the VMO
    // is zero-sized, and we handle it, or sizeof(line) < vmo_size.
    char line[kMaxInterpreterLineLen];
    memset(line, 0, sizeof(line));
    zx_status_t status = executable->read(line, 0, sizeof(line));
    if (status != ZX_OK) {
      report_error(err_msg, "error reading executable vmo: %d (%s)", status,
                   zx_status_get_string(status));
      return status;
    }

    // If no "#!" prefix is present, we're done; treat this as an ELF file and continue loading.
    if (line[0] != '#' || line[1] != '!') {
      break;
    }

    // Interpreter resolution is not allowed to carry on forever.
    if (depth == kMaxInterpreterDepth) {
      report_error(err_msg, "hit recursion limit resolving interpreters");
      return ZX_ERR_IO_INVALID;
    }

    // Find the end of the first line and NUL-terminate it to aid in parsing.
    char* line_end = reinterpret_cast<char*>(memchr(line, '\n', sizeof(line)));
    if (line_end) {
      *line_end = '\0';
    } else {
      // If there's no newline, then the script may be a single line and lack a trailing newline.
      // Look for the actual end of the script.
      line_end = reinterpret_cast<char*>(memchr(line, '\0', sizeof(line)));
      if (line_end == nullptr) {
        // This implies that the first line is longer than MAX_INTERPRETER_LINE_LEN.
        report_error(err_msg, "first line of script is too long");
        return ZX_ERR_OUT_OF_RANGE;
      }
    }
    size_t line_len = line_end - line;

    if (cpp20::starts_with(std::string_view(line, line_len), kResolvePrefix)) {
      // This is a "#!resolve" directive; use fuchsia.process.Resolve to resolve the name into a new
      // executable and appropriate loader.
      handled_resolve = true;
      if (handled_shebang) {
        report_error(err_msg, "already resolved a #! directive, mixing #!resolve is unsupported");
        return ZX_ERR_NOT_SUPPORTED;
      }

      char* name = &line[kResolvePrefix.size()];
      size_t name_len = line_len - kResolvePrefix.size();
      status = resolve_name(name, name_len, executable, ldsvc, err_msg);
      if (status != ZX_OK) {
        return status;
      }
    } else {
      // This is a general "#!" interpreter directive.
      handled_shebang = true;
      if (handled_resolve) {
        report_error(err_msg, "already resolved a #!resolve directive, mixing #! is unsupported");
        return ZX_ERR_NOT_SUPPORTED;
      }

      // Parse the interpreter spec to find the interpreter name and any args, and add those to
      // extra_args.
      char* interp_start;
      char* args_start;
      status = parse_interp_spec(line, &interp_start, &args_start);
      if (status != ZX_OK) {
        report_error(err_msg, "invalid #! interpreter spec");
        return status;
      }

      // args_start and interp_start are safe to treat as NUL terminated because parse_interp_spec
      // adds a NUL at the end of the interpreter name and we added an overall line NUL terminator
      // above when finding the line end.
      if (args_start != nullptr) {
        extra_args->emplace_front(args_start);
      }
      extra_args->emplace_front(interp_start);

      // Load the specified interpreter from the current namespace.
      char path_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
      status = load_path(interp_start, executable, path_msg);
      if (status != ZX_OK) {
        report_error(err_msg, "failed to load script interpreter '%s': %s", interp_start, path_msg);
        return status;
      }
    }
  }
  return ZX_OK;
}

// |SpawnActions| is a C++ helper that owns the resources referenced by a list
// of |fdio_spawn_action_t|, and offers range-based iteration.
class SpawnActions {
 public:
  SpawnActions(const fdio_spawn_action_t* actions, size_t action_count)
      : actions_(actions), action_count_(action_count) {}

  ~SpawnActions() {
    // |actions_| will be nullptr if the user did not supply any actions, or if
    // the actions ownership have been moved to a |ConsumingIterator|.
    if (!actions_)
      return;

    for (size_t i = 0; i < action_count_; ++i) {
      Free(actions_[i]);
    }
  }

  // Iterates through the list of actions without consuming them.
  const fdio_spawn_action_t* begin() const { return actions_; }
  const fdio_spawn_action_t* end() const { return &actions_[action_count_]; }

  // An iterator-style object that only allows traversing through the list of
  // spawn actions once. The contract is that the user consumes any resources
  // held by a particular action as they iterate over it.
  //
  // If the user did not finish iterating over all the actions, the iterator
  // will close any resources held in the remaining actions.
  class ConsumingIterator {
   public:
    ConsumingIterator(const fdio_spawn_action_t* actions, size_t action_count)
        : actions_(actions), action_count_(action_count) {}

    ~ConsumingIterator() {
      // |actions_| will be nullptr if the user did not supply any actions.
      if (!actions_)
        return;

      for (size_t i = used_; i < action_count_; ++i) {
        Free(actions_[i]);
      }
    }

    ConsumingIterator(const ConsumingIterator&) = delete;
    ConsumingIterator& operator=(ConsumingIterator&) = delete;
    ConsumingIterator(ConsumingIterator&&) = delete;
    ConsumingIterator& operator=(ConsumingIterator&&) = delete;

    bool has_next() const { return used_ < action_count_; }
    size_t index() const { return used_; }
    const fdio_spawn_action_t& operator*() const { return actions_[used_]; }
    const fdio_spawn_action_t* operator->() { return &actions_[used_]; }
    ConsumingIterator& operator++() {
      used_++;
      return *this;
    }

   private:
    const fdio_spawn_action_t* actions_;
    size_t action_count_;
    size_t used_ = 0;
  };

  // Converts the object into a consuming iterator. This transfers the resources
  // owned by |SpawnActions| into the iterator.
  ConsumingIterator ConsumeWhileIterating() && {
    auto* actions = actions_;
    actions_ = nullptr;
    if (action_count_ > 0) {
      ZX_DEBUG_ASSERT(actions != nullptr);
    }
    return ConsumingIterator(actions, action_count_);
  }

  // Frees the resources held in an |action|. If new spawn action types are
  // introduced that holds resources, corresponding cleanup logic should be
  // added here.
  static void Free(const fdio_spawn_action_t& action) {
    switch (action.action) {
      case FDIO_SPAWN_ACTION_ADD_NS_ENTRY:
        zx_handle_close(action.ns.handle);
        break;
      case FDIO_SPAWN_ACTION_ADD_HANDLE:
        zx_handle_close(action.h.handle);
        break;
      case FDIO_SPAWN_ACTION_TRANSFER_FD:
        close(action.fd.local_fd);
        break;
      default:
        break;
    }
  }

 private:
  const fdio_spawn_action_t* actions_;
  size_t action_count_;
};

// Bounds-checking helper for populating an array of |T| of predefined capacity.
template <typename T>
class Inserter {
 public:
  Inserter(T* data, size_t capacity) : data_(data), capacity_(capacity) {}

  T* AddNext() {
    ZX_DEBUG_ASSERT(used_ < capacity_);
    size_t idx = used_;
    used_++;
    return &data_[idx];
  }

  auto vector_view() { return fidl::VectorView<T>::FromExternal(data_, used_); }

  size_t used() const { return used_; }

 private:
  T* data_;
  size_t used_ = 0;
  size_t capacity_;
};

zx_status_t send_handles_and_namespace(const fidl::WireSyncClient<fprocess::Launcher>& launcher,
                                       size_t handle_capacity, uint32_t flags, zx_handle_t job,
                                       zx::channel ldsvc, zx_handle_t utc_clock, size_t name_count,
                                       fdio_flat_namespace_t* flat,
                                       SpawnActions::ConsumingIterator action, char* err_msg) {
  zx_status_t status = ZX_OK;
  // TODO(abarth): In principle, we should chunk array into separate
  // messages if we exceed ZX_CHANNEL_MAX_MSG_HANDLES.

  // VLAs cannot have zero size.
  fprocess::wire::HandleInfo handle_infos_storage[std::max(handle_capacity, 1UL)];
  fprocess::wire::NameInfo names_storage[std::max(name_count, 1UL)];
  // VLAs cannot be initialized.
  memset(handle_infos_storage, 0, sizeof(handle_infos_storage));
  memset(names_storage, 0, sizeof(names_storage));
  Inserter handle_infos(handle_infos_storage, handle_capacity);
  Inserter names(names_storage, name_count);

  std::bitset<FDIO_MAX_FD> fds_in_use;
  auto check_fd = [&fds_in_use](int fd) -> zx_status_t {
    fd &= ~FDIO_FLAG_USE_FOR_STDIO;
    if (fd < 0 || fd >= FDIO_MAX_FD) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    if (fds_in_use.test(fd)) {
      return ZX_ERR_ALREADY_EXISTS;
    }
    fds_in_use.set(fd);
    return ZX_OK;
  };

  if ((flags & FDIO_SPAWN_CLONE_JOB) != 0) {
    auto* handle_info = handle_infos.AddNext();
    handle_info->id = PA_JOB_DEFAULT;
    status =
        zx_handle_duplicate(job, ZX_RIGHT_SAME_RIGHTS, handle_info->handle.reset_and_get_address());
    if (status != ZX_OK) {
      report_error(err_msg, "failed to duplicate job: %d (%s)", status,
                   zx_status_get_string(status));
      return status;
    }
  }

  // ldsvc may be valid if flags contains FDIO_SPAWN_DEFAULT_LDSVC or if a ldsvc was obtained
  // through handling a '#!resolve' directive.
  if (ldsvc.is_valid()) {
    auto* handle_info = handle_infos.AddNext();
    handle_info->id = PA_LDSVC_LOADER;
    handle_info->handle = std::move(ldsvc);
  }

  for (; action.has_next(); ++action) {
    switch (action->action) {
      case FDIO_SPAWN_ACTION_CLONE_FD: {
        zx_handle_t fd_handle = ZX_HANDLE_INVALID;
        status = check_fd(action->fd.target_fd);
        if (status != ZX_OK) {
          report_error(err_msg, "invalid target %d to clone fd %d (action index %zu): %d",
                       action->fd.target_fd, action->fd.local_fd, action.index(), status);
          return status;
        }
        status = fdio_fd_clone(action->fd.local_fd, &fd_handle);
        if (status != ZX_OK) {
          report_error(err_msg, "failed to clone fd %d (action index %zu): %d", action->fd.local_fd,
                       action.index(), status);
          return status;
        }
        auto* handle_info = handle_infos.AddNext();
        handle_info->id = PA_HND(PA_FD, action->fd.target_fd);
        handle_info->handle.reset(fd_handle);
        break;
      }
      case FDIO_SPAWN_ACTION_TRANSFER_FD: {
        zx_handle_t fd_handle = ZX_HANDLE_INVALID;
        status = check_fd(action->fd.target_fd);
        if (status != ZX_OK) {
          report_error(err_msg, "invalid target %d to transfer fd %d (action index %zu): %d",
                       action->fd.target_fd, action->fd.local_fd, action.index(), status);
          return status;
        }
        status = fdio_fd_transfer(action->fd.local_fd, &fd_handle);
        if (status != ZX_OK) {
          report_error(err_msg, "failed to transfer fd %d (action index %zu): %d",
                       action->fd.local_fd, action.index(), status);
          return status;
        }
        auto* handle_info = handle_infos.AddNext();
        handle_info->id = PA_HND(PA_FD, action->fd.target_fd);
        handle_info->handle.reset(fd_handle);
        break;
      }
      case FDIO_SPAWN_ACTION_ADD_NS_ENTRY: {
        auto* name = names.AddNext();
        auto path = action->ns.prefix;
        name->path = fidl::StringView::FromExternal(path);
        name->directory = fidl::ClientEnd<fio::Directory>(zx::channel(action->ns.handle));
        break;
      }
      case FDIO_SPAWN_ACTION_ADD_HANDLE: {
        if (PA_HND_TYPE(action->h.id) == PA_FD) {
          int fd = PA_HND_ARG(action->h.id) & ~FDIO_FLAG_USE_FOR_STDIO;
          status = check_fd(fd);
          if (status != ZX_OK) {
            report_error(err_msg, "add-handle action has invalid fd %d (action index %zu): %d", fd,
                         action.index(), status);
            return status;
          }
        }
        auto* handle_info = handle_infos.AddNext();
        handle_info->id = action->h.id;
        handle_info->handle.reset(action->h.handle);
        break;
      }
      default: {
        break;
      }
    }
  }

  // Do these after generic actions so that actions can set these fds first.
  if ((flags & FDIO_SPAWN_CLONE_STDIO) != 0) {
    for (int fd = 0; fd < 3; ++fd) {
      if (fds_in_use.test(fd)) {
        // Skip a standard fd that was explicitly set by an action.
        continue;
      }
      zx_handle_t fd_handle = ZX_HANDLE_INVALID;
      status = fdio_fd_clone(fd, &fd_handle);
      if (status == ZX_ERR_INVALID_ARGS || status == ZX_ERR_NOT_SUPPORTED) {
        // This file descriptor is either closed, or something that doesn't
        // support cloning into a handle (e.g. a null fdio object).
        // We just skip it rather than generating an error.
        continue;
      }
      if (status != ZX_OK) {
        report_error(err_msg, "failed to clone fd %d: %d (%s)", fd, status,
                     zx_status_get_string(status));
        return status;
      }
      auto* handle_info = handle_infos.AddNext();
      handle_info->id = PA_HND(PA_FD, fd);
      handle_info->handle.reset(fd_handle);
    }
  }

  if ((flags & FDIO_SPAWN_CLONE_UTC_CLOCK) != 0) {
    if (utc_clock != ZX_HANDLE_INVALID) {
      auto* handle_info = handle_infos.AddNext();
      handle_info->id = PA_CLOCK_UTC;
      status = zx_handle_duplicate(
          utc_clock, ZX_RIGHT_READ | ZX_RIGHT_WAIT | ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER,
          handle_info->handle.reset_and_get_address());
      if (status != ZX_OK) {
        report_error(err_msg, "failed to clone UTC clock: %d (%s)", status,
                     zx_status_get_string(status));
        return status;
      }
    }
  }

  ZX_DEBUG_ASSERT(handle_infos.used() <= handle_capacity);
  status = launcher->AddHandles(handle_infos.vector_view()).status();
  if (status != ZX_OK) {
    report_error(err_msg, "failed to send handles: %d (%s)", status, zx_status_get_string(status));
    return status;
  }

  if (flat) {
    for (size_t i = 0; i < flat->count; i++) {
      auto* name = names.AddNext();
      auto path = flat->path[i];
      name->path = fidl::StringView::FromExternal(path);
      name->directory = fidl::ClientEnd<fio::Directory>(zx::channel(flat->handle[i]));
      flat->handle[i] = ZX_HANDLE_INVALID;
    }
  }

  ZX_DEBUG_ASSERT(names.used() == name_count);
  status = launcher->AddNames(names.vector_view()).status();
  if (status != ZX_OK) {
    report_error(err_msg, "failed send namespace: %d (%s)", status, zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

}  // namespace

__EXPORT
zx_status_t fdio_spawn(zx_handle_t job, uint32_t flags, const char* path, const char* const* argv,
                       zx_handle_t* process_out) {
  return fdio_spawn_etc(job, flags, path, argv, nullptr, 0, nullptr, process_out, nullptr);
}

__EXPORT
zx_status_t fdio_spawn_etc(zx_handle_t job, uint32_t flags, const char* path,
                           const char* const* argv, const char* const* explicit_environ,
                           size_t action_count, const fdio_spawn_action_t* actions,
                           zx_handle_t* process_out, char* err_msg) {
  zx::vmo executable;

  char path_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_status_t status = load_path(path, &executable, path_msg);

  if (status != ZX_OK) {
    report_error(err_msg, "failed to load executable from %s: %s", path, path_msg);
    // Set |err_msg| to nullptr to prevent |fdio_spawn_vmo| from generating
    // a less useful error message.
    err_msg = nullptr;
  }

  // Always call fdio_spawn_vmo to clean up arguments. If |executable| is
  // |ZX_HANDLE_INVALID|, then |fdio_spawn_vmo| will generate an error.
  zx_status_t spawn_status =
      fdio_spawn_vmo(job, flags, executable.release(), argv, explicit_environ, action_count,
                     actions, process_out, err_msg);

  // Use |status| if we already had an error before calling |fdio_spawn_vmo|.
  // Otherwise, we'll always return |ZX_ERR_INVALID_ARGS| rather than the more
  // useful status from |load_path|.
  return status != ZX_OK ? status : spawn_status;
}

namespace {

bool should_clone_namespace(std::string_view path, const std::vector<std::string_view>& prefixes) {
  return std::any_of(prefixes.begin(), prefixes.end(), [path](const std::string_view& prefix) {
    // Only share path if there is a directory prefix in |prefixes| that matches the path.
    // Also take care to not match partial directory names. Ex, /foo should not match
    // /foobar.
    return (cpp20::starts_with(path, prefix) &&
            (path.size() == prefix.size() || path[prefix.size()] == '/'));
  });
}

void filter_flat_namespace(fdio_flat_namespace_t* flat,
                           const std::vector<std::string_view>& prefixes) {
  size_t read, write;
  for (read = 0, write = 0; read < flat->count; ++read) {
    if (should_clone_namespace(flat->path[read], prefixes)) {
      if (read != write) {
        flat->handle[write] = flat->handle[read];
        flat->type[write] = flat->type[read];
        const_cast<const char**>(flat->path)[write] = flat->path[read];
      }
      write++;
    } else {
      zx_handle_close(flat->handle[read]);
      flat->handle[read] = ZX_HANDLE_INVALID;
    }
  }
  flat->count = write;
}

zx_status_t spawn_vmo_impl(zx_handle_t job, uint32_t flags, zx::vmo executable_vmo,
                           const char* const* argv, const char* const* explicit_environ,
                           SpawnActions& spawn_actions, zx_handle_t* process_out, char* err_msg) {
  // We intentionally don't fill in |err_msg| for invalid args.
  if (!executable_vmo.is_valid() || !argv) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (job == ZX_HANDLE_INVALID)
    job = zx_job_default();

  const char* process_name = argv[0];
  size_t process_name_size = 0;
  std::vector<std::string_view> shared_dirs;

  // Do a first pass over the actions and flags to calculate how many handles
  // and namespace entries to send. In the second pass later, we would allocate
  // data structures bespoke to that size.
  size_t handle_capacity = 0;
  size_t name_count = 0;
  for (const auto& action : spawn_actions) {
    switch (action.action) {
      case FDIO_SPAWN_ACTION_CLONE_FD:
      case FDIO_SPAWN_ACTION_TRANSFER_FD:
        ++handle_capacity;
        break;
      case FDIO_SPAWN_ACTION_ADD_NS_ENTRY:
        if (action.ns.handle == ZX_HANDLE_INVALID || !action.ns.prefix) {
          return ZX_ERR_INVALID_ARGS;
        }
        ++name_count;
        break;
      case FDIO_SPAWN_ACTION_ADD_HANDLE:
        if (action.h.handle == ZX_HANDLE_INVALID) {
          return ZX_ERR_INVALID_ARGS;
        }
        if (action.h.id == PA_CLOCK_UTC) {
          // A UTC Clock handle is explicitly passed in.
          if ((flags & FDIO_SPAWN_CLONE_UTC_CLOCK) != 0) {
            report_error(err_msg, "cannot clone global UTC clock and send explicit clock");
            return ZX_ERR_INVALID_ARGS;
          }
        }
        ++handle_capacity;
        break;
      case FDIO_SPAWN_ACTION_SET_NAME:
        if (action.name.data == nullptr) {
          return ZX_ERR_INVALID_ARGS;
        }
        process_name = action.name.data;
        break;
      case FDIO_SPAWN_ACTION_CLONE_DIR: {
        if (!action.dir.prefix) {
          return ZX_ERR_INVALID_ARGS;
        }
        // The path must be absolute (rooted at '/') and not contain a trailing '/', but do
        // allow the root namespace to be specified as "/".
        size_t len = strlen(action.dir.prefix);
        if (len == 0 || action.dir.prefix[0] != '/' ||
            (len > 1 && action.dir.prefix[len - 1] == '/')) {
          return ZX_ERR_INVALID_ARGS;
        }
        if (len == 1 && action.dir.prefix[0] == '/') {
          flags |= FDIO_SPAWN_CLONE_NAMESPACE;
        } else {
          shared_dirs.push_back(action.dir.prefix);
        }
      } break;
      default:
        break;
    }
  }

  if (!process_name) {
    return ZX_ERR_INVALID_ARGS;
  }

  if ((flags & FDIO_SPAWN_CLONE_JOB) != 0)
    ++handle_capacity;

  // Need to clone ldsvc here so it's available for handle_interpreters.
  zx::channel ldsvc;
  zx_status_t status = ZX_OK;
  if ((flags & FDIO_SPAWN_DEFAULT_LDSVC) != 0) {
    status = dl_clone_loader_service(ldsvc.reset_and_get_address());
    if (status != ZX_OK) {
      report_error(err_msg, "failed to clone library loader service: %d (%s)", status,
                   zx_status_get_string(status));
      return status;
    }
  }

  if ((flags & FDIO_SPAWN_CLONE_STDIO) != 0)
    handle_capacity += 3;

  zx_handle_t utc_clock = ZX_HANDLE_INVALID;
  if ((flags & FDIO_SPAWN_CLONE_UTC_CLOCK) != 0) {
    utc_clock = zx_utc_reference_get();
    if (utc_clock != ZX_HANDLE_INVALID) {
      ++handle_capacity;
    }
  }

  fprocess::wire::LaunchInfo launch_info = {
      .executable = std::move(executable_vmo),
  };
  std::list<std::string> extra_args;
  // resolve any '#!' directives that are present, updating executable and ldsvc as needed
  status = handle_interpreters(&launch_info.executable, &ldsvc, &extra_args, err_msg);
  if (status != ZX_OK) {
    return status;
  }
  if (ldsvc.is_valid()) {
    ++handle_capacity;
  }

  zx::result launcher_endpoints = fidl::CreateEndpoints<fprocess::Launcher>();
  if (!launcher_endpoints.is_ok()) {
    report_error(err_msg, "failed to create channel for launcher service: %d",
                 launcher_endpoints.status_value());
    return status;
  }
  fidl::WireSyncClient launcher{std::move(launcher_endpoints->client)};
  status = fdio_service_connect_by_name(fidl::DiscoverableProtocolName<fprocess::Launcher>,
                                        launcher_endpoints->server.TakeChannel().release());
  if (status != ZX_OK) {
    report_error(err_msg, "failed to connect to launcher service: %d (%s)", status,
                 zx_status_get_string(status));
    return status;
  }

  // send any extra arguments from handle_interpreters, then the normal arguments.
  {
    size_t capacity = extra_args.size();
    for (auto it = argv; *it; ++it) {
      ++capacity;
    }

    std::vector<fidl::VectorView<uint8_t>> args;
    args.reserve(capacity);
    for (const auto& arg : extra_args) {
      auto ptr = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(arg.data()));
      args.emplace_back(fidl::VectorView<uint8_t>::FromExternal(ptr, arg.length()));
    }
    for (auto it = argv; *it; ++it) {
      auto ptr = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(*it));
      args.emplace_back(fidl::VectorView<uint8_t>::FromExternal(ptr, strlen(*it)));
    }

    status =
        launcher->AddArgs(fidl::VectorView<fidl::VectorView<uint8_t>>::FromExternal(args)).status();
    if (status != ZX_OK) {
      report_error(err_msg, "failed to send argument vector: %d (%s)", status,
                   zx_status_get_string(status));
      return status;
    }
  }

  if (explicit_environ) {
    size_t capacity = 0;
    for (auto it = explicit_environ; *it; ++it) {
      ++capacity;
    }
    std::vector<fidl::VectorView<uint8_t>> env;
    env.reserve(capacity);
    for (auto it = explicit_environ; *it; ++it) {
      auto ptr = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(*it));
      env.emplace_back(fidl::VectorView<uint8_t>::FromExternal(ptr, strlen(*it)));
    }
    status = launcher->AddEnvirons(fidl::VectorView<fidl::VectorView<uint8_t>>::FromExternal(env))
                 .status();
    if (status != ZX_OK) {
      report_error(err_msg, "failed to send environment: %d (%s)", status,
                   zx_status_get_string(status));
      return status;
    }
  } else if ((flags & FDIO_SPAWN_CLONE_ENVIRON) != 0) {
    size_t capacity = 0;
    for (auto it = environ; *it; ++it) {
      ++capacity;
    }
    std::vector<fidl::VectorView<uint8_t>> env;
    env.reserve(capacity);
    for (auto it = environ; *it; ++it) {
      auto ptr = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(*it));
      env.emplace_back(fidl::VectorView<uint8_t>::FromExternal(ptr, strlen(*it)));
    }
    status = launcher->AddEnvirons(fidl::VectorView<fidl::VectorView<uint8_t>>::FromExternal(env))
                 .status();
    if (status != ZX_OK) {
      report_error(err_msg, "failed to send environment clone with FDIO_SPAWN_CLONE_ENVIRON: %d",
                   status);
      return status;
    }
  }

  fdio_flat_namespace_t* flat = nullptr;
  auto flat_namespace_cleanup = fit::defer([&flat] {
    if (flat) {
      fdio_ns_free_flat_ns(flat);
    }
  });

  if (!shared_dirs.empty() || (flags & FDIO_SPAWN_CLONE_NAMESPACE) != 0) {
    status = fdio_ns_export_root(&flat);
    if (status != ZX_OK) {
      report_error(err_msg, "Could not make copy of root namespace: %d (%s)", status,
                   zx_status_get_string(status));
      return status;
    }

    // If we don't clone the entire namespace, we need to filter down to only the
    // directories that are prefixed by paths in FDIO_SPAWN_ACTION_CLONE_DIR actions.
    if ((flags & FDIO_SPAWN_CLONE_NAMESPACE) == 0) {
      filter_flat_namespace(flat, shared_dirs);
    }

    name_count += flat->count;
  }

  status = send_handles_and_namespace(launcher, handle_capacity, flags, job, std::move(ldsvc),
                                      utc_clock, name_count, flat,
                                      std::move(spawn_actions).ConsumeWhileIterating(), err_msg);
  if (status != ZX_OK) {
    return status;
  }

  process_name_size = strlen(process_name);
  if (process_name_size >= ZX_MAX_NAME_LEN)
    process_name_size = ZX_MAX_NAME_LEN - 1;

  launch_info.name = fidl::StringView::FromExternal(process_name, process_name_size);
  status = zx_handle_duplicate(job, ZX_RIGHT_SAME_RIGHTS, launch_info.job.reset_and_get_address());
  if (status != ZX_OK) {
    report_error(err_msg, "failed to duplicate job handle: %d (%s)", status,
                 zx_status_get_string(status));
    return status;
  }

  fidl::WireResult reply = launcher->Launch(std::move(launch_info));
  status = reply.status();
  if (status != ZX_OK) {
    report_error(err_msg, "failed to send launch message: %d (%s)", status,
                 zx_status_get_string(status));
    return status;
  }

  status = reply.value().status;
  if (status != ZX_OK) {
    report_error(err_msg, "fuchsia.process.Launcher failed");
    return status;
  }

  // The launcher claimed to succeed but didn't actually give us a
  // process handle. Something is wrong with the launcher.
  if (!reply.value().process.is_valid()) {
    report_error(err_msg, "failed receive process handle");
    return ZX_ERR_BAD_HANDLE;
  }

  if (process_out) {
    *process_out = reply.value().process.release();
  }

  return ZX_OK;
}

}  // namespace

__EXPORT
zx_status_t fdio_spawn_vmo(zx_handle_t job, uint32_t flags, zx_handle_t executable_vmo,
                           const char* const* argv, const char* const* explicit_environ,
                           size_t action_count, const fdio_spawn_action_t* actions,
                           zx_handle_t* process_out, char* err_msg) {
  zx::vmo executable(executable_vmo);
  if (err_msg)
    err_msg[0] = '\0';

  if (action_count > 0 && !actions) {
    return ZX_ERR_INVALID_ARGS;
  }

  SpawnActions spawn_actions(actions, action_count);
  zx_status_t status = spawn_vmo_impl(job, flags, std::move(executable), argv, explicit_environ,
                                      spawn_actions, process_out, err_msg);

  // If we observe ZX_ERR_NOT_FOUND in the VMO spawn, it really means a
  // dependency of launching could not be fulfilled, but clients of spawn_etc
  // and friends could misinterpret this to mean the binary was not found.
  // Instead we remap that specific case to ZX_ERR_INTERNAL.
  if (status == ZX_ERR_NOT_FOUND) {
    return ZX_ERR_INTERNAL;
  }

  return status;
}
