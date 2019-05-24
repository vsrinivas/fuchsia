// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/memory_monitor/capture.h"

#include <fcntl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/zx/channel.h>
#include <lib/fdio/fdio.h>
#include <memory>
#include <src/lib/fxl/logging.h>
#include <task-utils/walker.h>
#include <zircon/process.h>
#include <zircon/status.h>

namespace memory {
namespace {

zx_status_t get_root_resource(zx_handle_t* root_resource) {
  const char* sysinfo = "/dev/misc/sysinfo";
  int fd = open(sysinfo, O_RDWR);
  if (fd < 0) {
    FXL_LOG(ERROR) << "Cannot open sysinfo: " << strerror(errno);
    return ZX_ERR_NOT_FOUND;
  }

  zx::channel channel;
  zx_status_t status =
      fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Cannot obtain sysinfo channel: "
                   << zx_status_get_string(status);
    return status;
  }

  zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetRootResource(
      channel.get(), &status, root_resource);
  if (fidl_status != ZX_OK) {
    FXL_LOG(ERROR) << "Cannot obtain root resource: "
                   << zx_status_get_string(fidl_status);
    return fidl_status;
  } else if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Cannot obtain root resource: "
                   << zx_status_get_string(status);
    return status;
  }
  return ZX_OK;
}
}  // namespace

class Capture::ProcessGetter final : public TaskEnumerator {
 public:
  ProcessGetter(CaptureLevel level, zx_koid_t self_koid, Capture& capture)
    : level_(level), self_koid_(self_koid), capture_(capture) {}

  zx_status_t OnProcess(
      int depth,
      zx_handle_t handle,
      zx_koid_t koid,
      zx_koid_t parent_koid) override {
    Process process = { .koid = koid };
    zx_status_t s = zx_object_get_property(
        handle, ZX_PROP_NAME, process.name, ZX_MAX_NAME_LEN);
    if (s != ZX_OK) {
      return s;
    }
    s = zx_object_get_info(handle, ZX_INFO_TASK_STATS,
                           &process.stats, sizeof(process.stats),
                           nullptr, nullptr);
    if (s != ZX_OK) {
      return s;
    }

    if (level_ == PROCESS) {
      return ZX_OK;
    }

    if (koid == self_koid_) {
      return ZX_OK;
    }

    size_t num_vmos;
    s = zx_object_get_info(handle, ZX_INFO_PROCESS_VMOS,
                           nullptr, 0, nullptr, &num_vmos);
    if (s != ZX_OK) {
      return s;
    }
    zx_info_vmo_t* vmos = new zx_info_vmo_t[num_vmos];
    s = zx_object_get_info(handle, ZX_INFO_PROCESS_VMOS,
                           vmos, num_vmos * sizeof(zx_info_vmo_t),
                           &num_vmos, nullptr);
    if (s != ZX_OK) {
      delete[] vmos;
      return s;
    }
    process.vmos.reserve(num_vmos);
    for (size_t i = 0; i < num_vmos; i++) {
      zx_koid_t vmo_koid = vmos[i].koid;
      capture_.koid_to_vmo_.emplace(vmo_koid, vmos[i]);
      process.vmos.push_back(vmo_koid);
    }
    delete[] vmos;
    capture_.koid_to_process_.emplace(koid, process);
    return ZX_OK;
  }

 private:
  CaptureLevel level_;
  zx_koid_t self_koid_;
  Capture& capture_;

  bool has_on_process() const final { return true; }
};

// static.
zx_status_t Capture::GetCaptureState(CaptureState& state) {
  zx_status_t err = get_root_resource(&state.root);
  if (err != ZX_OK) {
    return err;
  }

  zx_info_handle_basic_t info;
  err = zx_object_get_info(zx_process_self(), ZX_INFO_HANDLE_BASIC,
                           &info, sizeof(info), nullptr, nullptr);
  if (err != ZX_OK) {
    return err;
  }

  state.self_koid = info.koid;
  return ZX_OK;
}

// static.
zx_status_t Capture::GetCapture(
    Capture& capture, const CaptureState& state, CaptureLevel level) {
  capture.time_ = zx_clock_get_monotonic();
  zx_status_t err = zx_object_get_info(
      state.root, ZX_INFO_KMEM_STATS,
      &capture.kmem_, sizeof(capture.kmem_),
      NULL, NULL);
  if (err != ZX_OK) {
    return err;
  }

  if (level == KMEM) {
    return ZX_OK;
  }

  ProcessGetter getter(level, state.self_koid, capture);
  err = getter.WalkRootJobTree();
  if (err != ZX_OK) {
    return err;
  }
  return ZX_OK;
}

}  // namespace memory
