// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/process/process_builder.h"

#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/namespace.h>
#include <lib/sys/cpp/service_directory.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <array>
#include <string_view>

namespace process {

namespace {

// This should match the values used by zircon/system/ulib/fdio/spawn.c
constexpr size_t kFdioResolvePrefixLen = 10;
const char kFdioResolvePrefix[kFdioResolvePrefixLen + 1] = "#!resolve ";

// It is possible to setup an infinite loop of resolvers. We want to avoid this
// being a common abuse vector, but also stay out of the way of any complex user
// setups. This value is the same as spawn.c's above.
constexpr int kFdioMaxResolveDepth = 256;

}  // namespace

ProcessBuilder::ProcessBuilder(std::shared_ptr<sys::ServiceDirectory> services)
    : services_(services) {
  services_->Connect(launcher_.NewRequest());
}

ProcessBuilder::ProcessBuilder(zx::job job, std::shared_ptr<sys::ServiceDirectory> services)
    : ProcessBuilder(services) {
  launch_info_.job = std::move(job);
}

ProcessBuilder::~ProcessBuilder() = default;

void ProcessBuilder::LoadVMO(zx::vmo executable) {
  launch_info_.executable = std::move(executable);
}

zx_status_t ProcessBuilder::LoadPath(const std::string& path) {
  fbl::unique_fd fd;
  zx_status_t status = fdio_open_fd(path.c_str(),
                                    fuchsia::io::OPEN_RIGHT_READABLE |
                                    fuchsia::io::OPEN_RIGHT_EXECUTABLE,
                                    fd.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  zx::vmo executable_vmo;
  status = fdio_get_vmo_exec(fd.get(), executable_vmo.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  ::fidl::InterfaceHandle<::fuchsia::ldsvc::Loader> loader_iface;

  // Resolve VMOs containing #!resolve.
  fuchsia::process::ResolverSyncPtr resolver;  // Lazily bound.
  for (int i = 0; true; i++) {
    std::array<char, fuchsia::process::MAX_RESOLVE_NAME_SIZE + kFdioResolvePrefixLen> head;
    head.fill(0);
    status = executable_vmo.read(head.data(), 0, head.size());
    if (status != ZX_OK)
      return status;
    if (memcmp(kFdioResolvePrefix, head.data(), kFdioResolvePrefixLen) != 0)
      break;  // No prefix match.

    if (i == kFdioMaxResolveDepth)
      return ZX_ERR_IO_INVALID;  // Too much nesting.

    // The process name is after the prefix until the first newline.
    std::string_view name(&head[kFdioResolvePrefixLen], fuchsia::process::MAX_RESOLVE_NAME_SIZE);
    if (auto newline_index = name.rfind('\n'); newline_index != std::string_view::npos)
      name = name.substr(0, newline_index);

    // The resolver will give us a new VMO and loader to use.
    if (!resolver)
      services_->Connect(resolver.NewRequest());
    resolver->Resolve(std::string(name.data(), name.size()), &status, &executable_vmo,
                      &loader_iface);
    if (status != ZX_OK)
      return status;
  }

  // Save the loader info.
  zx::handle loader = loader_iface.TakeChannel();
  if (!loader) {
    // Resolver didn't give us a specific one, clone ours.
    status = dl_clone_loader_service(loader.reset_and_get_address());
    if (status != ZX_OK)
      return status;
  }
  AddHandle(PA_LDSVC_LOADER, std::move(loader));

  // Save the VMO info. Name it with the file part of the path.
  launch_info_.executable = std::move(executable_vmo);
  const char* name = path.c_str();
  if (path.length() >= ZX_MAX_NAME_LEN) {
    size_t offset = path.rfind('/');
    if (offset != std::string::npos) {
      name += offset + 1;
    }
  }
  launch_info_.executable.set_property(ZX_PROP_NAME, name, strlen(name));

  return ZX_OK;
}

void ProcessBuilder::AddArgs(const std::vector<std::string>& argv) {
  if (argv.empty())
    return;
  if (launch_info_.name.empty())
    launch_info_.name = argv[0];
  std::vector<std::vector<uint8_t>> args;
  for (const auto& arg : argv)
    args.push_back(std::vector<uint8_t>(arg.data(), arg.data() + arg.size()));
  launcher_->AddArgs(std::move(args));
}

void ProcessBuilder::AddHandle(uint32_t id, zx::handle handle) {
  handles_.push_back(fuchsia::process::HandleInfo{
      .handle = std::move(handle),
      .id = id,
  });
}

void ProcessBuilder::AddHandles(std::vector<fuchsia::process::HandleInfo> handles) {
  handles_.insert(handles_.end(), std::make_move_iterator(handles.begin()),
                  std::make_move_iterator(handles.end()));
}

void ProcessBuilder::SetDefaultJob(zx::job job) {
  handles_.push_back(fuchsia::process::HandleInfo{
      .handle = std::move(job),
      .id = PA_JOB_DEFAULT,
  });
}

void ProcessBuilder::SetName(std::string name) { launch_info_.name = std::move(name); }

void ProcessBuilder::CloneJob() {
  zx::job duplicate_job;
  if (launch_info_.job)
    launch_info_.job.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate_job);
  else
    zx::job::default_job()->duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate_job);
  SetDefaultJob(std::move(duplicate_job));
}

void ProcessBuilder::CloneNamespace() {
  fdio_flat_namespace_t* flat = nullptr;
  zx_status_t status = fdio_ns_export_root(&flat);
  if (status == ZX_OK) {
    std::vector<fuchsia::process::NameInfo> names;
    for (size_t i = 0; i < flat->count; ++i) {
      names.push_back(fuchsia::process::NameInfo{
          flat->path[i],
          fidl::InterfaceHandle<fuchsia::io::Directory>(zx::channel(flat->handle[i])),
      });
    }
    launcher_->AddNames(std::move(names));
  }
  free(flat);
}

void ProcessBuilder::CloneStdio() {
  // These file descriptors might be closed. Skip over erros cloning them.
  CloneFileDescriptor(STDIN_FILENO, STDIN_FILENO);
  CloneFileDescriptor(STDOUT_FILENO, STDOUT_FILENO);
  CloneFileDescriptor(STDERR_FILENO, STDERR_FILENO);
}

void ProcessBuilder::CloneEnvironment() {
  std::vector<std::vector<uint8_t>> env;
  for (size_t i = 0; environ[i]; ++i) {
    const char* var = environ[i];
    env.push_back(std::vector<uint8_t>(var, var + strlen(var)));
  }
  launcher_->AddEnvirons(std::move(env));
}

void ProcessBuilder::CloneAll() {
  CloneJob();
  CloneNamespace();
  CloneStdio();
  CloneEnvironment();
}

zx_status_t ProcessBuilder::CloneFileDescriptor(int local_fd, int target_fd) {
  zx::handle fd_handle;
  zx_status_t status = fdio_fd_clone(local_fd, fd_handle.reset_and_get_address());
  if (status != ZX_OK)
    return status;
  handles_.push_back(fuchsia::process::HandleInfo{
      .handle = std::move(fd_handle),
      .id = PA_HND(PA_FD, target_fd),
  });
  return ZX_OK;
}

zx_status_t ProcessBuilder::Prepare(std::string* error_message) {
  zx_status_t status = ZX_OK;
  launcher_->AddHandles(std::move(handles_));
  if (!launch_info_.job) {
    status = zx::job::default_job()->duplicate(ZX_RIGHT_SAME_RIGHTS, &launch_info_.job);
    if (status != ZX_OK)
      return status;
  }
  zx_status_t launcher_status = ZX_OK;
  fuchsia::process::ProcessStartDataPtr data;
  status = launcher_->CreateWithoutStarting(std::move(launch_info_), &launcher_status, &data);
  if (status != ZX_OK)
    return status;
  if (launcher_status != ZX_OK)
    return launcher_status;
  if (!data)
    return ZX_ERR_INVALID_ARGS;
  data_ = std::move(*data);
  return ZX_OK;
}

zx_status_t ProcessBuilder::Start(zx::process* process_out) {
  zx_status_t status = zx_process_start(data_.process.get(), data_.thread.get(), data_.entry,
                                        data_.stack, data_.bootstrap.release(), data_.vdso_base);
  if (status == ZX_OK && process_out)
    *process_out = std::move(data_.process);
  return status;
}

}  // namespace process
