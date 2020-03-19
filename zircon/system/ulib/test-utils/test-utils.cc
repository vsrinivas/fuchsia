// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/process/llcpp/fidl.h>
#include <lib/backtrace-request/backtrace-request.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>
#include <lib/zx/handle.h>
#include <lib/zx/job.h>
#include <lib/zx/vmo.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>

#include <cstdio>
#include <string>

#include <runtime/thread.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

#define TU_FAIL_ERRCODE 10

namespace fprocess = ::llcpp::fuchsia::process;

void* tu_malloc(size_t size) {
  void* result = malloc(size);
  if (result == NULL) {
    // TODO(dje): printf may try to malloc too ...
    unittest_printf_critical("out of memory trying to malloc(%zu)\n", size);
    exit(TU_FAIL_ERRCODE);
  }
  return result;
}

void* tu_calloc(size_t nmemb, size_t size) {
  void* result = calloc(nmemb, size);
  if (result == NULL) {
    // TODO(dje): printf may try to malloc too ...
    unittest_printf_critical("out of memory trying to calloc(%zu, %zu)\n", nmemb, size);
    exit(TU_FAIL_ERRCODE);
  }
  return result;
}

char* tu_strdup(const char* s) {
  size_t len = strlen(s) + 1;
  char* r = static_cast<char*>(tu_malloc(len));
  strcpy(r, s);
  return r;
}

char* tu_asprintf(const char* fmt, ...) {
  va_list args;
  char* result;
  va_start(args, fmt);
  if (vasprintf(&result, fmt, args) < 0) {
    unittest_printf_critical("out of memory trying to asprintf(%s)\n", fmt);
    exit(TU_FAIL_ERRCODE);
  }
  va_end(args);
  return result;
}

void tu_fatal(const char* what, zx_status_t status) {
  const char* reason = zx_status_get_string(status);
  unittest_printf_critical("\nFATAL: %s failed, rc %d (%s)\n", what, status, reason);

  // Request a backtrace to assist debugging.
  unittest_printf_critical("FATAL: backtrace follows:\n");
  unittest_printf_critical("       (using sw breakpoint request to crashlogger)\n");
  backtrace_request();

  unittest_printf_critical("FATAL: exiting process\n");
  exit(TU_FAIL_ERRCODE);
}

// Prints a message and terminates the process if |status| is not |ZX_OK|.
static void tu_check(const char* what, zx_status_t status) {
  if (status != ZX_OK) {
    tu_fatal(what, status);
  }
}

void tu_handle_close(zx_handle_t handle) {
  zx_status_t status = zx_handle_close(handle);
  // TODO(dje): It's still an open question as to whether errors other than ZX_ERR_BAD_HANDLE are
  // "advisory".
  if (status < 0) {
    tu_fatal(__func__, status);
  }
}

zx_handle_t tu_handle_duplicate(zx_handle_t handle) {
  zx_handle_t copy = ZX_HANDLE_INVALID;
  zx_status_t status = zx_handle_duplicate(handle, ZX_RIGHT_SAME_RIGHTS, &copy);
  if (status < 0)
    tu_fatal(__func__, status);
  return copy;
}

// N.B. This creates a C11 thread.
// See, e.g., musl/include/threads.h.

void tu_thread_create_c11(thrd_t* t, thrd_start_t entry, void* arg, const char* name) {
  int ret = thrd_create_with_name(t, entry, arg, name);
  if (ret != thrd_success) {
    // tu_fatal takes zx_status_t values.
    // The translation doesn't have to be perfect.
    switch (ret) {
      case thrd_nomem:
        tu_fatal(__func__, ZX_ERR_NO_MEMORY);
      default:
        tu_fatal(__func__, ZX_ERR_BAD_STATE);
    }
    __UNREACHABLE;
  }
}

zx_status_t tu_wait(uint32_t num_objects, const zx_handle_t* handles, const zx_signals_t* signals,
                    zx_signals_t* pending) {
  zx_wait_item_t items[num_objects];
  for (uint32_t n = 0; n < num_objects; n++) {
    items[n].handle = handles[n];
    items[n].waitfor = signals[n];
  }
  zx_status_t status = zx_object_wait_many(items, num_objects, ZX_TIME_INFINITE);
  for (uint32_t n = 0; n < num_objects; n++) {
    pending[n] = items[n].pending;
  }
  return status;
}

void tu_channel_create(zx_handle_t* handle0, zx_handle_t* handle1) {
  zx_handle_t handles[2];
  zx_status_t status = zx_channel_create(0, &handles[0], &handles[1]);
  if (status < 0)
    tu_fatal(__func__, status);
  *handle0 = handles[0];
  *handle1 = handles[1];
}

void tu_channel_write(zx_handle_t handle, uint32_t flags, const void* bytes, uint32_t num_bytes,
                      const zx_handle_t* handles, uint32_t num_handles) {
  zx_status_t status = zx_channel_write(handle, flags, bytes, num_bytes, handles, num_handles);
  if (status < 0)
    tu_fatal(__func__, status);
}

void tu_channel_read(zx_handle_t handle, uint32_t flags, void* bytes, uint32_t* num_bytes,
                     zx_handle_t* handles, uint32_t* num_handles) {
  zx_status_t status = zx_channel_read(handle, flags, bytes, handles, num_bytes ? *num_bytes : 0,
                                       num_handles ? *num_handles : 0, num_bytes, num_handles);
  if (status < 0)
    tu_fatal(__func__, status);
}

bool tu_channel_wait_readable(zx_handle_t channel) {
  zx_signals_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
  zx_signals_t pending;
  zx_status_t result = tu_wait(1, &channel, &signals, &pending);
  if (result != ZX_OK)
    tu_fatal(__func__, result);
  if ((pending & ZX_CHANNEL_READABLE) == 0) {
    unittest_printf("%s: peer closed\n", __func__);
    return false;
  }
  return true;
}

zx_handle_t tu_launch_process(zx_handle_t job, const char* name, int argc, const char* const* argv,
                              int envc, const char* const* envp, size_t num_handles,
                              zx_handle_t* handles, uint32_t* handle_ids) {
  springboard_t* sb =
      tu_launch_init(job, name, argc, argv, envc, envp, num_handles, handles, handle_ids);
  return tu_launch_fini(sb);
}

// Loads the executable at the given path into the given VMO.
static zx_status_t load_executable_vmo(const char* path, zx::vmo* result) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    return ZX_ERR_NOT_FOUND;
  }

  zx::vmo vmo;
  zx_status_t status = fdio_get_vmo_clone(fd, vmo.reset_and_get_address());
  close(fd);
  tu_check("load vmo from fd", status);

  status = vmo.replace_as_executable(zx::handle(), result);
  tu_check("replace vmo as executable", status);

  if (strlen(path) >= ZX_MAX_NAME_LEN) {
    const char* p = strrchr(path, '/');
    if (p != NULL) {
      path = p + 1;
    }
  }

  status = result->set_property(ZX_PROP_NAME, path, strlen(path));
  tu_check("setting vmo name", status);

  return ZX_OK;
}

struct springboard {
  fprocess::ProcessStartData data;

  springboard(fprocess::ProcessStartData* reference) {
    data.process.reset(reference->process.release());
    data.root_vmar.reset(reference->root_vmar.release());
    data.thread.reset(reference->thread.release());
    data.entry = reference->entry;
    data.stack = reference->stack;
    data.bootstrap.reset(reference->bootstrap.release());
    data.vdso_base = reference->vdso_base;
    // Not using base.
  }
};

zx_handle_t springboard_get_process_handle(springboard_t* sb) { return sb->data.process.get(); }

zx_handle_t springboard_get_root_vmar_handle(springboard_t* sb) { return sb->data.root_vmar.get(); }

springboard_t* tu_launch_init(zx_handle_t job, const char* name, int argc, const char* const* argv,
                              int envc, const char* const* envp, size_t num_handles,
                              zx_handle_t* handles, uint32_t* handle_ids) {
  zx_status_t status;

  // Connect to the Launcher service.

  zx::channel launcher_channel, launcher_request;
  status = zx::channel::create(0, &launcher_channel, &launcher_request);
  tu_check("creating channel for launcher service", status);

  std::string service_name = "/svc/" + std::string(fprocess::Launcher::Name);
  status = fdio_service_connect(service_name.c_str(), launcher_request.release());
  tu_check("connecting to launcher service", status);

  fprocess::Launcher::SyncClient launcher(std::move(launcher_channel));

  // Add arguments.

  {
    fidl::VectorView<uint8_t> data[argc];
    for (int i = 0; i < argc; i++) {
      data[i] = fidl::VectorView<uint8_t>(
          fidl::unowned_ptr(reinterpret_cast<uint8_t*>(const_cast<char*>(argv[i]))),
          strlen(argv[i]));
    }
    fidl::VectorView<fidl::VectorView<uint8_t>> args(fidl::unowned_ptr(data), argc);
    fprocess::Launcher::ResultOf::AddArgs result = launcher.AddArgs(std::move(args));
    tu_check("sending arguments", result.status());
  }

  // Add environment.

  if (envp) {
    fidl::VectorView<uint8_t> data[envc];
    for (int i = 0; i < envc; i++) {
      data[i] = fidl::VectorView<uint8_t>(
          fidl::unowned_ptr(reinterpret_cast<uint8_t*>(const_cast<char*>(envp[i]))),
          strlen(envp[i]));
    }
    fidl::VectorView<fidl::VectorView<uint8_t>> env(fidl::unowned_ptr(data), envc);
    fprocess::Launcher::ResultOf::AddEnvirons result = launcher.AddEnvirons(std::move(env));
    tu_check("sending environment", result.status());
  }

  // Add names.

  {
    fdio_flat_namespace_t* flat;
    zx_status_t status = fdio_ns_export_root(&flat);
    tu_check("getting namespace", status);
    size_t count = flat->count;
    fprocess::NameInfo data[count];
    for (size_t i = 0; i < count; i++) {
      const char* path = flat->path[i];
      data[i].path = fidl::unowned_str(path, strlen(path));
      data[i].directory.reset(flat->handle[i]);
    }
    fidl::VectorView<fprocess::NameInfo> names(fidl::unowned_ptr(data), count);
    fprocess::Launcher::ResultOf::AddNames result = launcher.AddNames(std::move(names));
    tu_check("sending names", result.status());
    free(flat);
  }

  // Add handles.

  {
    const size_t handle_count = num_handles + 1;
    fprocess::HandleInfo handle_infos[handle_count];

    // Input handles.

    size_t index;
    for (index = 0; index < num_handles; index++) {
      handle_infos[index].handle.reset(handles[index]);
      handle_infos[index].id = handle_ids[index];
    }

    // LDSVC

    zx::channel ldsvc;
    status = dl_clone_loader_service(handle_infos[index].handle.reset_and_get_address());
    tu_check("getting loader service", status);
    handle_infos[index++].id = PA_LDSVC_LOADER;

    fidl::VectorView<fprocess::HandleInfo> handle_vector(fidl::unowned_ptr(handle_infos),
                                                         handle_count);
    fprocess::Launcher::ResultOf::AddHandles result = launcher.AddHandles(std::move(handle_vector));
    tu_check("sending handles", result.status());
  }

  // Create the process.

  fprocess::LaunchInfo launch_info;

  const char* filename = argv[0];
  status = load_executable_vmo(filename, &launch_info.executable);
  tu_check("loading executable", status);

  if (job == ZX_HANDLE_INVALID) {
    job = zx_job_default();
  }
  zx::unowned_job unowned_job(job);
  status = unowned_job->duplicate(ZX_RIGHT_SAME_RIGHTS, &launch_info.job);
  tu_check("duplicating job for launch", status);

  const char* process_name = name ? name : filename;
  size_t process_name_size = strlen(process_name);
  if (process_name_size >= ZX_MAX_NAME_LEN) {
    process_name_size = ZX_MAX_NAME_LEN - 1;
  }
  launch_info.name = fidl::unowned_str(process_name, process_name_size);

  fprocess::Launcher::ResultOf::CreateWithoutStarting result =
      launcher.CreateWithoutStarting(std::move(launch_info));
  tu_check("process creation", result.status());

  fprocess::Launcher::CreateWithoutStartingResponse* response = result.Unwrap();
  tu_check("fuchsia.process.Launcher#CreateWithoutStarting failed", response->status);

  return new springboard(response->data.get());
}

void springboard_set_bootstrap(springboard_t* sb, zx_handle_t bootstrap) {
  sb->data.bootstrap.reset(bootstrap);
}

zx_handle_t tu_launch_fini(springboard* sb) {
  zx_handle_t process = sb->data.process.release();
  zx_handle_t thread = sb->data.thread.release();
  zx_status_t status = zx_process_start(process, thread, sb->data.entry, sb->data.stack,
                                        sb->data.bootstrap.release(), sb->data.vdso_base);
  zx_handle_close(thread);
  tu_check("starting process", status);
  delete sb;
  return process;
}

void tu_process_wait_signaled(zx_handle_t process) {
  zx_signals_t signals = ZX_PROCESS_TERMINATED;
  zx_signals_t pending;
  zx_status_t result = tu_wait(1, &process, &signals, &pending);
  if (result != ZX_OK)
    tu_fatal(__func__, result);
  if ((pending & ZX_PROCESS_TERMINATED) == 0) {
    unittest_printf_critical("%s: unexpected return from tu_wait\n", __func__);
    exit(TU_FAIL_ERRCODE);
  }
}

bool tu_process_has_exited(zx_handle_t process) {
  zx_info_process_t info;
  zx_status_t status;
  if ((status = zx_object_get_info(process, ZX_INFO_PROCESS, &info, sizeof(info), NULL, NULL)) < 0)
    tu_fatal("get process info", status);
  return info.exited;
}

int tu_process_get_return_code(zx_handle_t process) {
  zx_info_process_t info;
  zx_status_t status;
  if ((status = zx_object_get_info(process, ZX_INFO_PROCESS, &info, sizeof(info), NULL, NULL)) < 0)
    tu_fatal("get process info", status);
  if (!info.exited) {
    unittest_printf_critical("attempt to read return code of non-exited process");
    exit(TU_FAIL_ERRCODE);
  }
  return static_cast<int>(info.return_code);
}

int tu_process_wait_exit(zx_handle_t process) {
  tu_process_wait_signaled(process);
  return tu_process_get_return_code(process);
}

zx_handle_t tu_process_get_thread(zx_handle_t process, zx_koid_t tid) {
  zx_handle_t thread;
  zx_status_t status = zx_object_get_child(process, tid, ZX_RIGHT_SAME_RIGHTS, &thread);
  if (status == ZX_ERR_NOT_FOUND)
    return ZX_HANDLE_INVALID;
  if (status < 0)
    tu_fatal(__func__, status);
  return thread;
}

size_t tu_process_get_threads(zx_handle_t process, zx_koid_t* threads, size_t max_threads) {
  size_t num_threads;
  size_t buf_size = max_threads * sizeof(threads[0]);
  zx_status_t status =
      zx_object_get_info(process, ZX_INFO_PROCESS_THREADS, threads, buf_size, &num_threads, NULL);
  if (status < 0)
    tu_fatal(__func__, status);
  return num_threads;
}

zx_handle_t tu_job_create(zx_handle_t job) {
  zx_handle_t child_job;
  zx_status_t status = zx_job_create(job, 0, &child_job);
  if (status < 0)
    tu_fatal(__func__, status);
  return child_job;
}

zx_handle_t tu_io_port_create(void) {
  zx_handle_t handle;
  zx_status_t status = zx_port_create(0, &handle);
  if (status < 0)
    tu_fatal(__func__, status);
  return handle;
}

zx_handle_t tu_create_exception_channel(zx_handle_t task, uint32_t options) {
  zx_handle_t channel = ZX_HANDLE_INVALID;
  zx_status_t status = zx_task_create_exception_channel(task, options, &channel);
  if (status < 0)
    tu_fatal(__func__, status);
  return channel;
}

tu_exception_t tu_read_exception(zx_handle_t channel) {
  tu_exception_t exception;
  uint32_t num_bytes = sizeof(exception.info);
  uint32_t num_handles = 1;
  tu_channel_read(channel, 0, &exception.info, &num_bytes, &exception.exception, &num_handles);
  return exception;
}

zx_handle_t tu_exception_get_process(zx_handle_t exception) {
  zx_handle_t process = ZX_HANDLE_INVALID;
  zx_status_t status = zx_exception_get_process(exception, &process);
  if (status < 0)
    tu_fatal(__func__, status);
  return process;
}

zx_status_t tu_cleanup_breakpoint(zx_handle_t thread) {
#if defined(__x86_64__)
  // On x86, the pc is left at one past the s/w break insn,
  // so there's nothing more we need to do.
  return ZX_OK;
#elif defined(__aarch64__)
  // Skip past the brk instruction.
  zx_thread_state_general_regs_t regs = {};
  zx_status_t status =
      zx_thread_read_state(thread, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
  if (status != ZX_OK)
    return status;

  regs.pc += 4;
  return zx_thread_write_state(thread, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
#else
  return ZX_ERR_NOT_SUPPORTED;
#endif
}

zx_handle_t tu_exception_get_thread(zx_handle_t exception) {
  zx_handle_t thread = ZX_HANDLE_INVALID;
  zx_status_t status = zx_exception_get_thread(exception, &thread);
  if (status < 0)
    tu_fatal(__func__, status);
  return thread;
}

void tu_resume_from_exception(zx_handle_t exception_handle) {
  uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
  zx_status_t status =
      zx_object_set_property(exception_handle, ZX_PROP_EXCEPTION_STATE, &state, sizeof(state));
  if (status != ZX_OK)
    tu_fatal(__func__, status);

  status = zx_handle_close(exception_handle);
  if (status != ZX_OK)
    tu_fatal(__func__, status);
}

void tu_object_wait_async(zx_handle_t handle, zx_handle_t port, zx_signals_t signals) {
  uint64_t key = tu_get_koid(handle);
  uint32_t options = ZX_WAIT_ASYNC_ONCE;
  zx_status_t status = zx_object_wait_async(handle, port, key, signals, options);
  if (status < 0)
    tu_fatal(__func__, status);
}

void tu_handle_get_basic_info(zx_handle_t handle, zx_info_handle_basic_t* info) {
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, info, sizeof(*info), NULL, NULL);
  if (status < 0)
    tu_fatal(__func__, status);
}

zx_koid_t tu_get_koid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  tu_handle_get_basic_info(handle, &info);
  return info.koid;
}

zx_koid_t tu_get_related_koid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  tu_handle_get_basic_info(handle, &info);
  return info.related_koid;
}

zx_handle_t tu_get_thread(zx_handle_t proc, zx_koid_t tid) {
  zx_handle_t thread;
  zx_status_t status = zx_object_get_child(proc, tid, ZX_RIGHT_SAME_RIGHTS, &thread);
  if (status != ZX_OK)
    tu_fatal(__func__, status);
  return thread;
}

zx_info_thread_t tu_thread_get_info(zx_handle_t thread) {
  zx_info_thread_t info;
  zx_status_t status = zx_object_get_info(thread, ZX_INFO_THREAD, &info, sizeof(info), NULL, NULL);
  if (status < 0)
    tu_fatal("zx_object_get_info(ZX_INFO_THREAD)", status);
  return info;
}

uint32_t tu_thread_get_state(zx_handle_t thread) {
  zx_info_thread_t info = tu_thread_get_info(thread);
  return info.state;
}

bool tu_thread_is_dying_or_dead(zx_handle_t thread) {
  zx_info_thread_t info = tu_thread_get_info(thread);
  return (info.state == ZX_THREAD_STATE_DYING || info.state == ZX_THREAD_STATE_DEAD);
}

void tu_task_kill(zx_handle_t task) {
  zx_status_t status = zx_task_kill(task);
  if (status < 0)
    tu_fatal("zx_task_kill", status);
}

const char* tu_exception_to_string(uint32_t exception) {
  switch (exception) {
    case ZX_EXCP_GENERAL:
      return "ZX_EXCP_GENERAL";
    case ZX_EXCP_FATAL_PAGE_FAULT:
      return "ZX_EXCP_FATAL_PAGE_FAULT";
    case ZX_EXCP_UNDEFINED_INSTRUCTION:
      return "ZX_EXCP_UNDEFINED_INSTRUCTION";
    case ZX_EXCP_SW_BREAKPOINT:
      return "ZX_EXCP_SW_BREAKPOINT";
    case ZX_EXCP_HW_BREAKPOINT:
      return "ZX_EXCP_HW_BREAKPOINT";
    case ZX_EXCP_UNALIGNED_ACCESS:
      return "ZX_EXCP_UNALIGNED_ACCESS";
    case ZX_EXCP_THREAD_STARTING:
      return "ZX_EXCP_THREAD_STARTING";
    case ZX_EXCP_THREAD_EXITING:
      return "ZX_EXCP_THREAD_EXITING";
    case ZX_EXCP_POLICY_ERROR:
      return "ZX_EXCP_POLICY_ERROR";
    case ZX_EXCP_PROCESS_STARTING:
      return "ZX_EXCP_PROCESS_STARTING";
    default:
      break;
  }

  return "<unknown>";
}
