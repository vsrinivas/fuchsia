// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/process/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl/txn_header.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/dlfcn.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <list>
#include <string>
#include <utility>
#include <vector>

#include <fbl/unique_fd.h>

#include "private.h"

namespace fio = ::llcpp::fuchsia::io;

#define FDIO_RESOLVE_PREFIX "#!resolve "
#define FDIO_RESOLVE_PREFIX_LEN 10

// It is possible to setup an infinite loop of interpreters. We want to avoid this being a common
// abuse vector, but also stay out of the way of any complex user setups.
#define FDIO_SPAWN_MAX_INTERPRETER_DEPTH 255

// Maximum allowed length of a #! shebang directive.
// This applies to both types of #! directives - both the '#!resolve' special case and the general
// '#!' case with an arbitrary interpreter - but we use the fuchsia.process/Resolver limit rather
// than define a separate arbitrary limit.
#define FDIO_SPAWN_MAX_INTERPRETER_LINE_LEN \
  (fuchsia_process_MAX_RESOLVE_NAME_SIZE + FDIO_RESOLVE_PREFIX_LEN)
static_assert(FDIO_SPAWN_MAX_INTERPRETER_LINE_LEN < PAGE_SIZE,
              "max #! interpreter line length must be less than page size");

#define FDIO_SPAWN_LAUNCH_HANDLE_EXECUTABLE ((size_t)0u)
#define FDIO_SPAWN_LAUNCH_HANDLE_JOB ((size_t)1u)
#define FDIO_SPAWN_LAUNCH_HANDLE_COUNT ((size_t)2u)

#define FDIO_SPAWN_LAUNCH_REPLY_HANDLE_COUNT ((size_t)1u)

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

static void report_error(char* err_msg, const char* format, ...) {
  if (!err_msg)
    return;
  va_list args;
  va_start(args, format);
  vsnprintf(err_msg, FDIO_SPAWN_ERR_MSG_MAX_LENGTH, format, args);
  va_end(args);
}

static zx_status_t load_path(const char* path, zx::vmo* out_vmo, char* err_msg) {
  fbl::unique_fd fd;
  zx_status_t status = fdio_open_fd(path, fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
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
    if (p != NULL) {
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

static void measure_cstring_array(const char* const* array, size_t* count_out, size_t* len_out) {
  size_t i = 0;
  size_t len = 0;
  while (array[i]) {
    len += FIDL_ALIGN(strlen(array[i]));
    ++i;
  }
  *count_out = i;
  *len_out = len;
}

// resolve_name makes a call to the fuchsia.process.Resolver service and may return a vmo and
// associated loader service, if the name resolves within the current realm.
static zx_status_t resolve_name(const char* name, size_t name_len, zx::vmo* out_executable,
                                zx::channel* out_ldsvc, char* err_msg) {
  zx::channel resolver, resolver_request;
  zx_status_t status = zx::channel::create(0, &resolver, &resolver_request);
  if (status != ZX_OK) {
    report_error(err_msg, "failed to create channel: %d", status);
    return ZX_ERR_INTERNAL;
  }

  status = fdio_service_connect("/svc/fuchsia.process.Resolver", resolver_request.release());
  if (status != ZX_OK) {
    report_error(err_msg, "failed to connect to resolver service: %d", status);
    return ZX_ERR_INTERNAL;
  }

  zx_status_t io_status = fuchsia_process_ResolverResolve(resolver.get(), name, name_len, &status,
                                                          out_executable->reset_and_get_address(),
                                                          out_ldsvc->reset_and_get_address());
  if (io_status != ZX_OK) {
    report_error(err_msg, "failed to send resolver request: %d", io_status);
    return ZX_ERR_INTERNAL;
  }

  if (status != ZX_OK) {
    report_error(err_msg, "failed to resolve %.*s", name_len, name);
  }
  return status;
}

// Find the starting point of the interpreter and the interpreter arguments in a #! script header.
// Note that the input buffer (line) will be modified to add a NUL after the interpreter name.
static zx_status_t parse_interp_spec(char* line, char** interp_start, char** args_start) {
  *args_start = NULL;

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
// Directives will be resolved until none are detected, an error is encounted, or a resolution limit
// is reached. Also, mixing the two types is unsupported.
//
// The executable and ldsvc paramters are both inputs to and outputs from this function, and are
// updated based on the resolved directives. executable must always be valid, and ldsvc must be
// valid at minimum for the 2nd case above, though it should generally always be valid as well when
// calling this.
static zx_status_t handle_interpreters(zx::vmo* executable, zx::channel* ldsvc,
                                       std::list<std::string>* extra_args, char* err_msg) {
  extra_args->clear();

  // Mixing #!resolve and general #! within a single spawn is unsupported so that the #!
  // interpreters can simply be loaded from the current namespace.
  bool handled_resolve = false;
  bool handled_shebang = false;
  for (size_t depth = 0; true; ++depth) {
    // VMO sizes are page aligned and MAX_INTERPRETER_LINE_LEN < PAGE_SIZE (asserted above), so
    // there's no use in checking VMO size explicitly here. Either the read fails because the VMO is
    // zero-sized, and we handle it, or sizeof(line) < vmo_size.
    char line[FDIO_SPAWN_MAX_INTERPRETER_LINE_LEN];
    memset(line, 0, sizeof(line));
    zx_status_t status = executable->read(line, 0, sizeof(line));
    if (status != ZX_OK) {
      report_error(err_msg, "error reading executable vmo: %d", status);
      return status;
    }

    // If no "#!" prefix is present, we're done; treat this as an ELF file and continue loading.
    if (line[0] != '#' || line[1] != '!') {
      break;
    }

    // Interpreter resolution is not allowed to carry on forever.
    if (depth == FDIO_SPAWN_MAX_INTERPRETER_DEPTH) {
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
      if (line_end == NULL) {
        // This implies that the first line is longer than MAX_INTERPRETER_LINE_LEN.
        report_error(err_msg, "first line of script is too long");
        return ZX_ERR_OUT_OF_RANGE;
      }
    }
    size_t line_len = line_end - line;

    if (memcmp(FDIO_RESOLVE_PREFIX, line, FDIO_RESOLVE_PREFIX_LEN) == 0) {
      // This is a "#!resolve" directive; use fuchsia.process.Resolve to resolve the name into a new
      // executable and appropriate loader.
      handled_resolve = true;
      if (handled_shebang) {
        report_error(err_msg, "already resolved a #! directive, mixing #!resolve is unsupported");
        return ZX_ERR_NOT_SUPPORTED;
      }

      char* name = &line[FDIO_RESOLVE_PREFIX_LEN];
      size_t name_len = line_len - FDIO_RESOLVE_PREFIX_LEN;
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
      if (args_start != NULL) {
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

static zx_status_t send_cstring_array(const zx::channel& launcher, uint64_t ordinal,
                                      const char* const* array) {
  size_t count = 0;
  size_t len = 0;

  // TODO(abarth): In principle, we should chunk array into separate
  // messages if we exceed ZX_CHANNEL_MAX_MSG_BYTES.
  measure_cstring_array(array, &count, &len);

  if (count == 0)
    return ZX_OK;

  size_t msg_len = sizeof(fidl_message_header_t) + sizeof(fidl_vector_t) +
                   count * sizeof(fidl_vector_t) + FIDL_ALIGN(len);
  uint8_t msg[msg_len];
  memset(msg, 0, msg_len);

  fidl_message_header_t* hdr = (fidl_message_header_t*)msg;
  fidl_vector_t* vector = (fidl_vector_t*)hdr + 1;
  fidl_vector_t* bytes = (fidl_vector_t*)(vector + 1);
  uint8_t* payload = (uint8_t*)(bytes + count);

  fidl_init_txn_header(hdr, 0, ordinal);
  vector->count = count;
  vector->data = (void*)FIDL_ALLOC_PRESENT;

  size_t offset = 0;
  for (size_t i = 0; i < count; ++i) {
    size_t size = strlen(array[i]);
    bytes[i].count = size;
    bytes[i].data = (void*)FIDL_ALLOC_PRESENT;
    memcpy(payload + offset, array[i], size);
    offset += FIDL_ALIGN(size);
  }

  return launcher.write(0, msg, static_cast<uint32_t>(msg_len), NULL, 0);
}

static zx_status_t send_handles(const zx::channel& launcher, size_t handle_capacity, uint32_t flags,
                                zx_handle_t job, zx::channel ldsvc, size_t action_count,
                                const fdio_spawn_action_t* actions, char* err_msg) {
  // TODO(abarth): In principle, we should chunk array into separate
  // messages if we exceed ZX_CHANNEL_MAX_MSG_HANDLES.

  size_t msg_capacity = sizeof(fuchsia_process_LauncherAddHandlesRequest) +
                        FIDL_ALIGN(handle_capacity * sizeof(fuchsia_process_HandleInfo));
  uint8_t msg[msg_capacity];
  memset(msg, 0, msg_capacity);

  fuchsia_process_LauncherAddHandlesRequest* req = (fuchsia_process_LauncherAddHandlesRequest*)msg;
  fuchsia_process_HandleInfo* handle_infos = (fuchsia_process_HandleInfo*)(req + 1);

  zx_handle_t handles[handle_capacity];

  memset(handles, 0, sizeof(handles));

  fidl_init_txn_header(&req->hdr, 0, fuchsia_process_LauncherAddHandlesGenOrdinal);

  zx_status_t status = ZX_OK;
  uint32_t h = 0;
  size_t a = 0;
  size_t msg_len = 0;

  if ((flags & FDIO_SPAWN_CLONE_JOB) != 0) {
    handle_infos[h].handle = FIDL_HANDLE_PRESENT;
    handle_infos[h].id = PA_JOB_DEFAULT;
    status = zx_handle_duplicate(job, ZX_RIGHT_SAME_RIGHTS, &handles[h++]);
    if (status != ZX_OK) {
      report_error(err_msg, "failed to duplicate job: %d", status);
      goto cleanup;
    }
  }

  // ldsvc may be valid if flags contains FDIO_SPAWN_DEFAULT_LDSVC or if a ldsvc was obtained
  // through handling a '#!resolve' directive.
  if (ldsvc.is_valid()) {
    handle_infos[h].handle = FIDL_HANDLE_PRESENT;
    handle_infos[h].id = PA_LDSVC_LOADER;
    handles[h++] = ldsvc.release();
  }

  if ((flags & FDIO_SPAWN_CLONE_STDIO) != 0) {
    for (int fd = 0; fd < 3; ++fd) {
      zx_handle_t fd_handle = ZX_HANDLE_INVALID;
      status = fdio_fd_clone(fd, &fd_handle);
      if (status == ZX_ERR_INVALID_ARGS || status == ZX_ERR_NOT_SUPPORTED) {
        // This file descriptor is either closed, or something that doesn't
        // support cloning into a handle (e.g. a null fdio object).
        // We just skip it rather than generating an error.
        continue;
      }
      if (status != ZX_OK) {
        report_error(err_msg, "failed to clone fd %d: %d", fd, status);
        goto cleanup;
      }
      handle_infos[h].handle = FIDL_HANDLE_PRESENT;
      handle_infos[h].id = PA_HND(PA_FD, fd);
      handles[h++] = fd_handle;
    }
  }

  for (; a < action_count; ++a) {
    zx_handle_t fd_handle = ZX_HANDLE_INVALID;

    switch (actions[a].action) {
      case FDIO_SPAWN_ACTION_CLONE_FD:
        status = fdio_fd_clone(actions[a].fd.local_fd, &fd_handle);
        if (status != ZX_OK) {
          report_error(err_msg, "failed to clone fd %d (action index %zu): %d",
                       actions[a].fd.local_fd, a, status);
          goto cleanup;
        }
        break;
      case FDIO_SPAWN_ACTION_TRANSFER_FD:
        status = fdio_fd_transfer(actions[a].fd.local_fd, &fd_handle);
        if (status != ZX_OK) {
          report_error(err_msg, "failed to transfer fd %d (action index %zu): %d",
                       actions[a].fd.local_fd, a, status);
          goto cleanup;
        }
        break;
      case FDIO_SPAWN_ACTION_ADD_HANDLE:
        handle_infos[h].handle = FIDL_HANDLE_PRESENT;
        handle_infos[h].id = actions[a].h.id;
        handles[h++] = actions[a].h.handle;
        continue;
      default:
        continue;
    }

    handle_infos[h].handle = FIDL_HANDLE_PRESENT;
    handle_infos[h].id = PA_HND(PA_FD, actions[a].fd.target_fd);
    handles[h++] = fd_handle;
  }

  req->handles.count = h;
  req->handles.data = (void*)FIDL_ALLOC_PRESENT;

  ZX_DEBUG_ASSERT(h <= handle_capacity);

  msg_len = sizeof(fuchsia_process_LauncherAddHandlesRequest) +
            FIDL_ALIGN(h * sizeof(fuchsia_process_HandleInfo));
  status = launcher.write(0, msg, static_cast<uint32_t>(msg_len), handles, h);

  if (status != ZX_OK)
    report_error(err_msg, "failed send handles: %d", status);

  return status;

cleanup:
  zx_handle_close_many(handles, h);

  // If |a| is less than |action_count|, that means we encountered an error
  // before we processed all the actions. We need to iterate through the rest
  // of the table and close the file descriptors and handles that we're
  // supposed to consume.
  for (size_t i = a; i < action_count; ++i) {
    switch (actions[i].action) {
      case FDIO_SPAWN_ACTION_TRANSFER_FD:
        close(actions[i].fd.local_fd);
        break;
      case FDIO_SPAWN_ACTION_ADD_HANDLE:
        zx_handle_close(actions[i].h.handle);
        break;
    }
  }

  return status;
}

static zx_status_t send_namespace(const zx::channel& launcher, size_t name_count, size_t name_len,
                                  fdio_flat_namespace_t* flat, size_t action_count,
                                  const fdio_spawn_action_t* actions, char* err_msg) {
  size_t msg_len = sizeof(fuchsia_process_LauncherAddNamesRequest) +
                   FIDL_ALIGN(name_count * sizeof(fuchsia_process_NameInfo)) + FIDL_ALIGN(name_len);
  uint8_t msg[msg_len];
  memset(msg, 0, msg_len);

  fuchsia_process_LauncherAddNamesRequest* req = (fuchsia_process_LauncherAddNamesRequest*)msg;
  fuchsia_process_NameInfo* names = (fuchsia_process_NameInfo*)(req + 1);
  uint8_t* payload = (uint8_t*)(names + name_count);

  zx_handle_t handles[name_count];

  memset(handles, 0, sizeof(handles));

  fidl_init_txn_header(&req->hdr, 0, fuchsia_process_LauncherAddNamesGenOrdinal);
  req->names.count = name_count;
  req->names.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);

  size_t n = 0;
  uint32_t h = 0;
  size_t offset = 0;

  if (flat) {
    while (n < flat->count) {
      size_t size = strlen(flat->path[n]);
      names[n].path.size = size;
      names[n].path.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
      names[n].directory = FIDL_HANDLE_PRESENT;
      memcpy(payload + offset, flat->path[n], size);
      offset += FIDL_ALIGN(size);
      handles[h++] = flat->handle[n];
      flat->handle[n] = ZX_HANDLE_INVALID;
      n++;
    }
  }

  for (size_t i = 0; i < action_count; ++i) {
    if (actions[i].action == FDIO_SPAWN_ACTION_ADD_NS_ENTRY) {
      size_t size = strlen(actions[i].ns.prefix);
      names[n].path.size = size;
      names[n].path.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
      names[n].directory = FIDL_HANDLE_PRESENT;
      memcpy(payload + offset, actions[i].ns.prefix, size);
      offset += FIDL_ALIGN(size);
      handles[h++] = actions[i].ns.handle;
      n++;
    }
  }

  ZX_DEBUG_ASSERT(n == name_count);
  ZX_DEBUG_ASSERT(h == name_count);

  zx_status_t status = launcher.write(0, msg, static_cast<uint32_t>(msg_len), handles, h);

  if (status != ZX_OK)
    report_error(err_msg, "failed send namespace: %d", status);

  return status;
}

__EXPORT
zx_status_t fdio_spawn(zx_handle_t job, uint32_t flags, const char* path, const char* const* argv,
                       zx_handle_t* process_out) {
  return fdio_spawn_etc(job, flags, path, argv, NULL, 0, NULL, process_out, NULL);
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
    // Set |err_msg| to NULL to prevent |fdio_spawn_vmo| from generating
    // a less useful error message.
    err_msg = NULL;
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

static bool should_clone_namespace(std::string_view path,
                                   const std::vector<std::string_view>& prefixes) {
  for (const auto& prefix : prefixes) {
    // Only share path if there is a directory prefix in |prefixes| that matches the path.
    // Also take care to not match partial directory names. Ex, /foo should not match
    // /foobar.
    if (path.compare(0, prefix.size(), prefix) == 0 &&
        (path.size() == prefix.size() || path[prefix.size()] == '/')) {
      return true;
    }
  }
  return false;
}

static void filter_flat_namespace(fdio_flat_namespace_t* flat,
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

__EXPORT
zx_status_t fdio_spawn_vmo(zx_handle_t job, uint32_t flags, zx_handle_t executable_vmo,
                           const char* const* argv, const char* const* explicit_environ,
                           size_t action_count, const fdio_spawn_action_t* actions,
                           zx_handle_t* process_out, char* err_msg) {
  zx_status_t status = ZX_OK;
  fdio_flat_namespace_t* flat = NULL;
  size_t name_count = 0;
  size_t name_len = 0;
  size_t handle_capacity = 0;
  std::vector<std::string_view> shared_dirs;
  zx::channel launcher;
  zx::channel launcher_request;
  zx_handle_t msg_handles[FDIO_SPAWN_LAUNCH_HANDLE_COUNT];
  zx::channel ldsvc;
  const char* process_name = NULL;
  size_t process_name_size = 0;
  std::list<std::string> extra_args;
  zx::vmo executable(executable_vmo);
  executable_vmo = ZX_HANDLE_INVALID;
  bool handle_interpreters_returned_not_found = false;

  memset(msg_handles, 0, sizeof(msg_handles));

  if (err_msg)
    err_msg[0] = '\0';

  // We intentionally don't fill in |err_msg| for invalid args.

  if (!executable.is_valid() || !argv || (action_count != 0 && !actions)) {
    status = ZX_ERR_INVALID_ARGS;
    goto cleanup;
  }

  if (job == ZX_HANDLE_INVALID)
    job = zx_job_default();

  process_name = argv[0];

  for (size_t i = 0; i < action_count; ++i) {
    switch (actions[i].action) {
      case FDIO_SPAWN_ACTION_CLONE_FD:
      case FDIO_SPAWN_ACTION_TRANSFER_FD:
        ++handle_capacity;
        break;
      case FDIO_SPAWN_ACTION_ADD_NS_ENTRY:
        if (actions[i].ns.handle == ZX_HANDLE_INVALID || !actions[i].ns.prefix) {
          status = ZX_ERR_INVALID_ARGS;
          goto cleanup;
        }
        ++name_count;
        name_len += FIDL_ALIGN(strlen(actions[i].ns.prefix));
        break;
      case FDIO_SPAWN_ACTION_ADD_HANDLE:
        if (actions[i].h.handle == ZX_HANDLE_INVALID) {
          status = ZX_ERR_INVALID_ARGS;
          goto cleanup;
        }
        ++handle_capacity;
        break;
      case FDIO_SPAWN_ACTION_SET_NAME:
        if (actions[i].name.data == NULL) {
          status = ZX_ERR_INVALID_ARGS;
          goto cleanup;
        }
        process_name = actions[i].name.data;
        break;
      case FDIO_SPAWN_ACTION_CLONE_DIR: {
        if (!actions[i].dir.prefix) {
          status = ZX_ERR_INVALID_ARGS;
          goto cleanup;
        }
        // The path must be absolute (rooted at '/') and not contain a trailing '/', but do
        // allow the root namespace to be specified as "/".
        size_t len = strlen(actions[i].dir.prefix);
        if (len == 0 || actions[i].dir.prefix[0] != '/' ||
            (len > 1 && actions[i].dir.prefix[len - 1] == '/')) {
          status = ZX_ERR_INVALID_ARGS;
          goto cleanup;
        } else if (len == 1 && actions[i].dir.prefix[0] == '/') {
          flags |= FDIO_SPAWN_CLONE_NAMESPACE;
        } else {
          shared_dirs.push_back(actions[i].dir.prefix);
        }
      } break;
      default:
        break;
    }
  }

  if (!process_name) {
    status = ZX_ERR_INVALID_ARGS;
    goto cleanup;
  }

  if ((flags & FDIO_SPAWN_CLONE_JOB) != 0)
    ++handle_capacity;

  // Need to clone ldsvc here so it's available for handle_interpreters.
  if ((flags & FDIO_SPAWN_DEFAULT_LDSVC) != 0) {
    status = dl_clone_loader_service(ldsvc.reset_and_get_address());
    if (status != ZX_OK) {
      report_error(err_msg, "failed to clone library loader service: %d", status);
      goto cleanup;
    }
  }

  if ((flags & FDIO_SPAWN_CLONE_STDIO) != 0)
    handle_capacity += 3;

  if (!shared_dirs.empty() || (flags & FDIO_SPAWN_CLONE_NAMESPACE) != 0) {
    status = fdio_ns_export_root(&flat);
    if (status != ZX_OK) {
      report_error(err_msg, "Could not make copy of root namespace: %d", status);
      goto cleanup;
    }

    // If we don't clone the entire namespace, we need to filter down to only the
    // directories that are prefixed by paths in FDIO_SPAWN_ACTION_CLONE_DIR actions.
    if ((flags & FDIO_SPAWN_CLONE_NAMESPACE) == 0) {
      filter_flat_namespace(flat, shared_dirs);
    }

    name_count += flat->count;
    for (size_t i = 0; i < flat->count; ++i) {
      name_len += FIDL_ALIGN(strlen(flat->path[i]));
    }
  }

  // resolve any '#!' directives that are present, updating executable and ldsvc as needed
  status = handle_interpreters(&executable, &ldsvc, &extra_args, err_msg);
  if (status != ZX_OK) {
    handle_interpreters_returned_not_found = (status == ZX_ERR_NOT_FOUND);
    goto cleanup;
  }
  if (ldsvc.is_valid()) {
    ++handle_capacity;
  }

  status = zx::channel::create(0, &launcher, &launcher_request);
  if (status != ZX_OK) {
    report_error(err_msg, "failed to create channel for process launcher: %d", status);
    goto cleanup;
  }

  status = fdio_service_connect("/svc/fuchsia.process.Launcher", launcher_request.release());
  if (status != ZX_OK) {
    report_error(err_msg, "failed to connect to launcher service: %d", status);
    goto cleanup;
  }

  // send any extra arguments from handle_interpreters, then the normal arguments
  if (!extra_args.empty()) {
    std::vector<const char*> extra_argv;
    extra_argv.reserve(extra_args.size() + 1);
    for (const auto& arg : extra_args) {
      extra_argv.push_back(arg.c_str());
    }
    extra_argv.push_back(nullptr);

    status =
        send_cstring_array(launcher, fuchsia_process_LauncherAddArgsGenOrdinal, extra_argv.data());
    if (status != ZX_OK) {
      report_error(err_msg, "failed to send extra argument vector: %d", status);
      goto cleanup;
    }
  }
  status = send_cstring_array(launcher, fuchsia_process_LauncherAddArgsGenOrdinal, argv);
  if (status != ZX_OK) {
    report_error(err_msg, "failed to send argument vector: %d", status);
    goto cleanup;
  }

  if (explicit_environ) {
    status =
        send_cstring_array(launcher, fuchsia_process_LauncherAddEnvironsGenOrdinal, explicit_environ);
    if (status != ZX_OK) {
      report_error(err_msg, "failed to send environment: %d", status);
      goto cleanup;
    }
  } else if ((flags & FDIO_SPAWN_CLONE_ENVIRON) != 0) {
    status = send_cstring_array(launcher, fuchsia_process_LauncherAddEnvironsGenOrdinal,
                                (const char* const*)environ);
    if (status != ZX_OK) {
      report_error(err_msg, "failed to send environment clone with FDIO_SPAWN_CLONE_ENVIRON: %d",
                   status);
      goto cleanup;
    }
  }

  if (handle_capacity) {
    status = send_handles(launcher, handle_capacity, flags, job, std::move(ldsvc), action_count,
                          actions, err_msg);
    if (status != ZX_OK) {
      // When |send_handles| fails, it consumes all the action handles
      // that it knows about, but it doesn't consume the handles used for
      // |FDIO_SPAWN_ACTION_ADD_NS_ENTRY|.

      for (size_t i = 0; i < action_count; ++i) {
        switch (actions[i].action) {
          case FDIO_SPAWN_ACTION_ADD_NS_ENTRY:
            zx_handle_close(actions[i].ns.handle);
            break;
          default:
            break;
        }
      }

      action_count = 0;  // We've now consumed all the handles.
      goto cleanup;
    }
  }

  if (name_count) {
    status = send_namespace(launcher, name_count, name_len, flat, action_count, actions, err_msg);
    if (status != ZX_OK) {
      action_count = 0;
      goto cleanup;
    }
  }

  action_count = 0;  // We've consumed all the actions at this point.

  process_name_size = strlen(process_name);
  if (process_name_size >= ZX_MAX_NAME_LEN)
    process_name_size = ZX_MAX_NAME_LEN - 1;

  {
    struct {
      FIDL_ALIGNDECL
      fuchsia_process_LauncherLaunchRequest req;
      uint8_t process_name[FIDL_ALIGN(ZX_MAX_NAME_LEN)];
    } msg;

    memset(&msg, 0, sizeof(msg));
    size_t msg_len = sizeof(fuchsia_process_LauncherLaunchRequest) + FIDL_ALIGN(process_name_size);

    fidl_init_txn_header(&msg.req.hdr, 0, fuchsia_process_LauncherLaunchGenOrdinal);
    msg.req.info.executable = FIDL_HANDLE_PRESENT;
    msg.req.info.job = FIDL_HANDLE_PRESENT;
    msg.req.info.name.size = process_name_size;
    msg.req.info.name.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
    memcpy(msg.process_name, process_name, process_name_size);

    msg_handles[FDIO_SPAWN_LAUNCH_HANDLE_EXECUTABLE] = executable.release();

    status =
        zx_handle_duplicate(job, ZX_RIGHT_SAME_RIGHTS, &msg_handles[FDIO_SPAWN_LAUNCH_HANDLE_JOB]);
    if (status != ZX_OK) {
      report_error(err_msg, "failed to duplicate job handle: %d", status);
      goto cleanup;
    }

    fuchsia_process_LauncherLaunchResponse reply;

    zx_handle_t process = ZX_HANDLE_INVALID;

    memset(&reply, 0, sizeof(reply));

    zx_channel_call_args_t args;
    args.wr_bytes = &msg;
    args.wr_handles = msg_handles;
    args.rd_bytes = &reply;
    args.rd_handles = &process;
    args.wr_num_bytes = static_cast<uint32_t>(msg_len);
    args.wr_num_handles = FDIO_SPAWN_LAUNCH_HANDLE_COUNT;
    args.rd_num_bytes = sizeof(reply);
    args.rd_num_handles = FDIO_SPAWN_LAUNCH_REPLY_HANDLE_COUNT;

    uint32_t actual_bytes = 0;
    uint32_t actual_handles = 0;

    status = launcher.call(0, zx::time::infinite(), &args, &actual_bytes, &actual_handles);

    // zx_channel_call always consumes handles.
    memset(msg_handles, 0, sizeof(msg_handles));

    if (status != ZX_OK) {
      report_error(err_msg, "failed to send launch message: %d", status);
      goto cleanup;
    }

    status = reply.status;

    if (status == ZX_OK) {
      // The launcher claimed to succeed but didn't actually give us a
      // process handle. Something is wrong with the launcher.
      if (process == ZX_HANDLE_INVALID) {
        status = ZX_ERR_BAD_HANDLE;
        report_error(err_msg, "failed receive process handle");
        // This jump skips over closing the process handle, but that's
        // fine because we didn't receive a process handle.
        goto cleanup;
      }

      if (process_out) {
        *process_out = process;
        process = ZX_HANDLE_INVALID;
      }
    } else {
      report_error(err_msg, "fuchsia.process.Launcher failed");
    }

    if (process != ZX_HANDLE_INVALID)
      zx_handle_close(process);
  }

cleanup:
  if (actions) {
    for (size_t i = 0; i < action_count; ++i) {
      switch (actions[i].action) {
        case FDIO_SPAWN_ACTION_ADD_NS_ENTRY:
          zx_handle_close(actions[i].ns.handle);
          break;
        case FDIO_SPAWN_ACTION_ADD_HANDLE:
          zx_handle_close(actions[i].h.handle);
          break;
        default:
          break;
      }
    }
  }

  if (flat) {
    fdio_ns_free_flat_ns(flat);
  }

  if (msg_handles[FDIO_SPAWN_LAUNCH_HANDLE_EXECUTABLE] != ZX_HANDLE_INVALID)
    zx_handle_close(msg_handles[FDIO_SPAWN_LAUNCH_HANDLE_EXECUTABLE]);

  if (msg_handles[FDIO_SPAWN_LAUNCH_HANDLE_JOB] != ZX_HANDLE_INVALID)
    zx_handle_close(msg_handles[FDIO_SPAWN_LAUNCH_HANDLE_JOB]);

  // If we observe ZX_ERR_NOT_FOUND in the VMO spawn, it really means a
  // dependency of launching could not be fulfilled, but clients of spawn_etc
  // and friends could misinterpret this to mean the binary was not found.
  // Instead we remap that specific case to ZX_ERR_INTERNAL.
  if (status == ZX_ERR_NOT_FOUND && !handle_interpreters_returned_not_found) {
    return ZX_ERR_INTERNAL;
  }

  return status;
}
