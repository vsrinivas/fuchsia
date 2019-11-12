// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs-manager.h"

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/zircon-internal/debug.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <memory>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>

#include "cobalt-client/cpp/collector.h"
#include "lib/async/cpp/task.h"
#include "metrics.h"

#define ZXDEBUG 0

namespace devmgr {

cobalt_client::CollectorOptions FsManager::CollectorOptions() {
  cobalt_client::CollectorOptions options = cobalt_client::CollectorOptions::GeneralAvailability();
  options.project_name = "local_storage";
  options.initial_response_deadline = zx::msec(10);
  options.response_deadline = zx::usec(10);
  return options;
}

FsManager::FsManager(zx::event fshost_event, FsHostMetrics metrics)
    : event_(std::move(fshost_event)),
      global_loop_(new async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread)),
      registry_(global_loop_.get()),
      metrics_(std::move(metrics)) {
  ZX_ASSERT(global_root_ == nullptr);
}

// In the event that we haven't been explicitly signalled, tear ourself down.
FsManager::~FsManager() {
  if (global_shutdown_.has_handler()) {
    event_.signal(0, FSHOST_SIGNAL_EXIT);
    auto deadline = zx::deadline_after(zx::sec(2));
    zx_signals_t pending;
    event_.wait_one(FSHOST_SIGNAL_EXIT_DONE, deadline, &pending);
  }
}

zx_status_t FsManager::Create(zx::event fshost_event, FsHostMetrics metrics,
                              std::unique_ptr<FsManager>* out) {
  auto fs_manager =
      std::unique_ptr<FsManager>(new FsManager(std::move(fshost_event), std::move(metrics)));
  zx_status_t status = fs_manager->Initialize();
  if (status != ZX_OK) {
    return status;
  }
  *out = std::move(fs_manager);
  return ZX_OK;
}

zx_status_t FsManager::Initialize() {
  uint64_t physmem_size = zx_system_get_physmem();
  ZX_DEBUG_ASSERT(physmem_size % PAGE_SIZE == 0);
  size_t page_limit = physmem_size / PAGE_SIZE;

  zx_status_t status = memfs::Vfs::Create("<root>", page_limit, &root_vfs_, &global_root_);
  if (status != ZX_OK) {
    return status;
  }

  fbl::RefPtr<fs::Vnode> vn;
  if ((status = global_root_->Create(&vn, "boot", S_IFDIR)) != ZX_OK) {
    return status;
  }
  if ((status = global_root_->Create(&vn, "tmp", S_IFDIR)) != ZX_OK) {
    return status;
  }
  for (unsigned n = 0; n < fbl::count_of(kMountPoints); n++) {
    auto open_result = root_vfs_->Open(global_root_, fbl::StringPiece(kMountPoints[n]),
                                       fs::VnodeConnectionOptions::ReadWrite().set_create(),
                                       fs::Rights::ReadWrite(), S_IFDIR);
    if (open_result.is_error()) {
      return open_result.error();
    }
    ZX_ASSERT(open_result.is_ok());
    mount_nodes[n] = std::move(open_result.ok().vnode);
  }

  global_loop_->StartThread("root-dispatcher");
  root_vfs_->SetDispatcher(global_loop_->dispatcher());
  return ZX_OK;
}

void FsManager::FlushMetrics() { mutable_metrics()->FlushUntilSuccess(global_loop_->dispatcher()); }

zx_status_t FsManager::InstallFs(const char* path, zx::channel h) {
  for (unsigned n = 0; n < fbl::count_of(kMountPoints); n++) {
    if (!strcmp(path, kMountPoints[n])) {
      return root_vfs_->InstallRemote(mount_nodes[n], fs::MountChannel(std::move(h)));
    }
  }
  return ZX_ERR_NOT_FOUND;
}

zx_status_t FsManager::ServeRoot(zx::channel server) {
  fs::Rights rights;
  rights.read = true;
  rights.write = true;
  rights.admin = true;
  rights.execute = true;
  return root_vfs_->ServeDirectory(global_root_, std::move(server), rights);
}

void FsManager::WatchExit() {
  global_shutdown_.set_handler([this](async_dispatcher_t* dispatcher, async::Wait* wait,
                                      zx_status_t status, const zx_packet_signal_t* signal) {
    root_vfs_->UninstallAll(ZX_TIME_INFINITE);
    event_.signal(0, FSHOST_SIGNAL_EXIT_DONE);
  });

  global_shutdown_.set_object(event_.get());
  global_shutdown_.set_trigger(FSHOST_SIGNAL_EXIT);
  global_shutdown_.Begin(global_loop_->dispatcher());
}

}  // namespace devmgr
