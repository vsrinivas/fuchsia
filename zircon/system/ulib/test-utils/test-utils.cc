// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.process/cpp/wire.h>
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

#include <fbl/unique_fd.h>
#include <runtime/thread.h>
#include <test-utils/test-utils.h>

#define TU_FAIL_ERRCODE 10

namespace fprocess = fuchsia_process;
namespace fio = fuchsia_io;

void tu_fatal(const char* what, zx_status_t status) {
  const char* reason = zx_status_get_string(status);
  printf("\nFATAL: %s failed, rc %d (%s)\n", what, status, reason);

  // Request a backtrace to assist debugging.
  printf("FATAL: backtrace follows:\n");
  printf("       (using sw breakpoint request to crashlogger)\n");
  backtrace_request();

  printf("FATAL: exiting process\n");
  exit(TU_FAIL_ERRCODE);
}

// Prints a message and terminates the process if |status| is not |ZX_OK|.
static void tu_check(const char* what, zx_status_t status) {
  if (status != ZX_OK) {
    tu_fatal(what, status);
  }
}

bool tu_channel_wait_readable(zx_handle_t channel) {
  zx_signals_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
  zx_signals_t pending;
  zx_status_t result = zx_object_wait_one(channel, signals, ZX_TIME_INFINITE, &pending);
  if (result != ZX_OK)
    tu_fatal(__func__, result);
  if ((pending & ZX_CHANNEL_READABLE) == 0) {
    printf("%s: peer closed\n", __func__);
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
  fbl::unique_fd fd;
  zx_status_t status =
      fdio_open_fd(path, fio::wire::kOpenRightReadable | fio::wire::kOpenRightExecutable,
                   fd.reset_and_get_address());
  tu_check("open executable fd", status);

  status = fdio_get_vmo_exec(fd.get(), result->reset_and_get_address());
  tu_check("get exec vmo from fd", status);

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
  fprocess::wire::ProcessStartData data;

  springboard(fprocess::wire::ProcessStartData* reference) {
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

  status = fdio_service_connect(fidl::DiscoverableProtocolDefaultPath<fprocess::Launcher>,
                                launcher_request.release());
  tu_check("connecting to launcher service", status);

  fidl::WireSyncClient<fprocess::Launcher> launcher(std::move(launcher_channel));

  // Add arguments.

  {
    fidl::VectorView<uint8_t> data[argc];
    for (int i = 0; i < argc; i++) {
      data[i] = fidl::VectorView<uint8_t>::FromExternal(
          reinterpret_cast<uint8_t*>(const_cast<char*>(argv[i])), strlen(argv[i]));
    }
    auto args = fidl::VectorView<fidl::VectorView<uint8_t>>::FromExternal(data, argc);
    fidl::WireResult<fprocess::Launcher::AddArgs> result = launcher.AddArgs(std::move(args));
    tu_check("sending arguments", result.status());
  }

  // Add environment.

  if (envp) {
    fidl::VectorView<uint8_t> data[envc];
    for (int i = 0; i < envc; i++) {
      data[i] = fidl::VectorView<uint8_t>::FromExternal(
          reinterpret_cast<uint8_t*>(const_cast<char*>(envp[i])), strlen(envp[i]));
    }
    auto env = fidl::VectorView<fidl::VectorView<uint8_t>>::FromExternal(data, envc);
    fidl::WireResult<fprocess::Launcher::AddEnvirons> result = launcher.AddEnvirons(std::move(env));
    tu_check("sending environment", result.status());
  }

  // Add names.

  {
    fdio_flat_namespace_t* flat;
    zx_status_t status = fdio_ns_export_root(&flat);
    tu_check("getting namespace", status);
    size_t count = flat->count;
    fprocess::wire::NameInfo data[count];
    for (size_t i = 0; i < count; i++) {
      const char* path = flat->path[i];
      data[i].path = fidl::StringView::FromExternal(path);
      data[i].directory = fidl::ClientEnd<fio::Directory>(zx::channel(flat->handle[i]));
    }
    auto names = fidl::VectorView<fprocess::wire::NameInfo>::FromExternal(data, count);
    fidl::WireResult<fprocess::Launcher::AddNames> result = launcher.AddNames(std::move(names));
    tu_check("sending names", result.status());
    free(flat);
  }

  // Add handles.

  {
    const size_t handle_count = num_handles + 1;
    fprocess::wire::HandleInfo handle_infos[handle_count];

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

    auto handle_vector =
        fidl::VectorView<fprocess::wire::HandleInfo>::FromExternal(handle_infos, handle_count);
    fidl::WireResult<fprocess::Launcher::AddHandles> result =
        launcher.AddHandles(std::move(handle_vector));
    tu_check("sending handles", result.status());
  }

  // Create the process.

  fprocess::wire::LaunchInfo launch_info;

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
  launch_info.name = fidl::StringView::FromExternal(process_name, process_name_size);

  fidl::WireResult<fprocess::Launcher::CreateWithoutStarting> result =
      launcher.CreateWithoutStarting(std::move(launch_info));
  tu_check("process creation", result.status());

  fidl::WireResponse<fprocess::Launcher::CreateWithoutStarting>* response = result.Unwrap();
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

void tu_launch_abort(springboard* sb) { delete sb; }

void tu_process_wait_signaled(zx_handle_t process) {
  zx_signals_t signals = ZX_PROCESS_TERMINATED;
  zx_signals_t pending;
  zx_status_t result = zx_object_wait_one(process, signals, ZX_TIME_INFINITE, &pending);
  if (result != ZX_OK)
    tu_fatal(__func__, result);
  if ((pending & ZX_PROCESS_TERMINATED) == 0) {
    printf("%s: unexpected return from zx_object_wait_one\n", __func__);
    exit(TU_FAIL_ERRCODE);
  }
}

int tu_process_get_return_code(zx_handle_t process) {
  zx_info_process_t info;
  zx_status_t status;
  if ((status = zx_object_get_info(process, ZX_INFO_PROCESS, &info, sizeof(info), NULL, NULL)) < 0)
    tu_fatal("get process info", status);
  if (!(info.flags & ZX_INFO_PROCESS_FLAG_EXITED)) {
    printf("attempt to read return code of non-exited process");
    exit(TU_FAIL_ERRCODE);
  }
  return static_cast<int>(info.return_code);
}

void tu_object_wait_async(zx_handle_t handle, zx_handle_t port, zx_signals_t signals) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    tu_fatal(__func__, status);
  }
  uint64_t key = info.koid;
  uint32_t options = 0;
  status = zx_object_wait_async(handle, port, key, signals, options);
  if (status < 0)
    tu_fatal(__func__, status);
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
