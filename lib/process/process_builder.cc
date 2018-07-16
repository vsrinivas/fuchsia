// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/process/process_builder.h"

#include <fcntl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/util.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include "lib/component/cpp/environment_services.h"

namespace process {

ProcessBuilder::ProcessBuilder() {
  component::ConnectToEnvironmentService(launcher_.NewRequest());
}

ProcessBuilder::ProcessBuilder(zx::job job) : ProcessBuilder() {
  launch_info_.job = std::move(job);
}

ProcessBuilder::~ProcessBuilder() = default;

void ProcessBuilder::LoadVMO(zx::vmo executable) {
  launch_info_.executable = std::move(executable);
}

zx_status_t ProcessBuilder::LoadPath(const std::string& path) {
  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0)
    return ZX_ERR_IO;
  zx_status_t status =
      fdio_get_vmo_clone(fd, launch_info_.executable.reset_and_get_address());
  close(fd);

  if (status == ZX_OK) {
    const char* name = path.c_str();

    if (path.length() >= ZX_MAX_NAME_LEN) {
      size_t offset = path.rfind('/');
      if (offset != std::string::npos) {
        name += offset + 1;
      }
    }

    launch_info_.executable.set_property(ZX_PROP_NAME, name, strlen(name));
  }

  return status;
}

void ProcessBuilder::AddArgs(const std::vector<std::string>& argv) {
  if (argv.empty())
    return;
  if (launch_info_.name->empty())
    launch_info_.name.reset(argv[0]);
  fidl::VectorPtr<fidl::StringPtr> args;
  for (const auto& arg : argv)
    args.push_back(arg);
  launcher_->AddArgs(std::move(args));
}

void ProcessBuilder::AddHandle(uint32_t id, zx::handle handle) {
  handles_.push_back(fuchsia::process::HandleInfo{
      .id = id,
      .handle = std::move(handle),
  });
}

void ProcessBuilder::AddHandles(
    std::vector<fuchsia::process::HandleInfo> handles) {
  handles_->insert(handles_->end(), std::make_move_iterator(handles.begin()),
                   std::make_move_iterator(handles.end()));
}

void ProcessBuilder::SetDefaultJob(zx::job job) {
  handles_.push_back(fuchsia::process::HandleInfo{
      .id = PA_JOB_DEFAULT,
      .handle = std::move(job),
  });
}

void ProcessBuilder::SetName(std::string name) {
  launch_info_.name.reset(std::move(name));
}

void ProcessBuilder::CloneJob() {
  zx::job duplicate_job;
  if (launch_info_.job)
    launch_info_.job.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate_job);
  else
    zx::job::default_job()->duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate_job);
  SetDefaultJob(std::move(duplicate_job));
}

void ProcessBuilder::CloneLdsvc() {
  fuchsia::process::HandleInfo handle_info;
  handle_info.id = PA_LDSVC_LOADER;
  zx_status_t status =
      dl_clone_loader_service(handle_info.handle.reset_and_get_address());
  ZX_ASSERT(status == ZX_OK);
  handles_.push_back(std::move(handle_info));
}

void ProcessBuilder::CloneNamespace() {
  fdio_flat_namespace_t* flat = nullptr;
  zx_status_t status = fdio_ns_export_root(&flat);
  if (status == ZX_OK) {
    fidl::VectorPtr<fuchsia::process::NameInfo> names;
    for (size_t i = 0; i < flat->count; ++i) {
      names.push_back(fuchsia::process::NameInfo{
          .path = flat->path[i],
          .directory = zx::channel(flat->handle[i]),
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
  fidl::VectorPtr<fidl::StringPtr> env;
  for (size_t i = 0; environ[i]; ++i)
    env.push_back(environ[i]);
  launcher_->AddEnvirons(std::move(env));
}

void ProcessBuilder::CloneAll() {
  CloneJob();
  CloneLdsvc();
  CloneNamespace();
  CloneStdio();
  CloneEnvironment();
}

zx_status_t ProcessBuilder::CloneFileDescriptor(int local_fd, int target_fd) {
  zx_handle_t fdio_handles[FDIO_MAX_HANDLES];
  uint32_t fdio_types[FDIO_MAX_HANDLES];
  zx_status_t status =
      fdio_clone_fd(local_fd, target_fd, fdio_handles, fdio_types);
  if (status < ZX_OK)
    return status;
  for (int i = 0; i < status; ++i) {
    handles_.push_back(fuchsia::process::HandleInfo{
        .id = fdio_types[i],
        .handle = zx::handle(fdio_handles[i]),
    });
  }
  return ZX_OK;
}

zx_status_t ProcessBuilder::Prepare(std::string* error_message) {
  zx_status_t status = ZX_OK;
  launcher_->AddHandles(std::move(handles_));
  if (!launch_info_.job) {
    status = zx::job::default_job()->duplicate(ZX_RIGHT_SAME_RIGHTS,
                                               &launch_info_.job);
    if (status != ZX_OK)
      return status;
  }
  fuchsia::process::CreateWithoutStartingResult result;
  status = launcher_->CreateWithoutStarting(std::move(launch_info_), &result);
  if (status != ZX_OK)
    return status;
  if (result.status != ZX_OK) {
    if (error_message)
      *error_message = result.error_message;
    return result.status;
  }
  if (!result.data)
    return ZX_ERR_INVALID_ARGS;
  data_ = std::move(*result.data);
  return ZX_OK;
}

zx_status_t ProcessBuilder::Start(zx::process* process_out) {
  zx_status_t status =
      zx_process_start(data_.process.get(), data_.thread.get(), data_.entry,
                       data_.sp, data_.bootstrap.release(), data_.vdso_base);
  if (status == ZX_OK && process_out)
    *process_out = std::move(data_.process);
  return status;
}

}  // namespace process
