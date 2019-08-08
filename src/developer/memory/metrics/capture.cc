// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/metrics/capture.h"

#include <fcntl.h>
#include <fuchsia/boot/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <zircon/process.h>
#include <zircon/status.h>

#include <memory>

#include <src/lib/fxl/logging.h>
#include <task-utils/walker.h>
#include <trace/event.h>

namespace memory {

class OSImpl : public OS, public TaskEnumerator {
 private:
  zx_status_t GetRootResource(zx_handle_t* root_resource) override {
    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      return status;
    }
    const char* root_resource_svc = "/svc/fuchsia.boot.RootResource";
    status = fdio_service_connect(root_resource_svc, remote.release());
    if (status != ZX_OK) {
      return status;
    }

    return fuchsia_boot_RootResourceGet(local.get(), root_resource);
  }

  zx_handle_t ProcessSelf() override { return zx_process_self(); }
  zx_time_t GetMonotonic() override { return zx_clock_get_monotonic(); }

  zx_status_t GetProcesses(
      fit::function<zx_status_t(int, zx_handle_t, zx_koid_t, zx_koid_t)> cb) override {
    cb_ = std::move(cb);
    return WalkRootJobTree();
  }

  zx_status_t OnProcess(int depth, zx_handle_t handle, zx_koid_t koid,
                        zx_koid_t parent_koid) override {
    return cb_(depth, handle, koid, parent_koid);
  }

  zx_status_t GetProperty(zx_handle_t handle, uint32_t property, void* value,
                          size_t name_len) override {
    return zx_object_get_property(handle, property, value, name_len);
  }

  zx_status_t GetInfo(zx_handle_t handle, uint32_t topic, void* buffer, size_t buffer_size,
                      size_t* actual, size_t* avail) override {
    TRACE_DURATION("memory_metrics", "OSImpl::GetInfo", "topic", topic, "buffer_size", buffer_size);
    return zx_object_get_info(handle, topic, buffer, buffer_size, actual, avail);
  }

  bool has_on_process() const final { return true; }

  fit::function<zx_status_t(int /* depth */, zx_handle_t /* handle */, zx_koid_t /* koid */,
                            zx_koid_t /* parent_koid */)>
      cb_;
};

// static.
zx_status_t Capture::GetCaptureState(CaptureState& state) {
  OSImpl osImpl;
  return GetCaptureState(state, osImpl);
}

zx_status_t Capture::GetCaptureState(CaptureState& state, OS& os) {
  zx_status_t err = os.GetRootResource(&state.root);
  if (err != ZX_OK) {
    return err;
  }

  zx_info_handle_basic_t info;
  err = os.GetInfo(os.ProcessSelf(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (err != ZX_OK) {
    return err;
  }

  state.self_koid = info.koid;
  return ZX_OK;
}

// static.
zx_status_t Capture::GetCapture(Capture& capture, const CaptureState& state, CaptureLevel level) {
  OSImpl osImpl;
  return GetCapture(capture, state, level, osImpl);
}

zx_status_t Capture::GetCapture(Capture& capture, const CaptureState& state, CaptureLevel level,
                                OS& os) {
  TRACE_DURATION("memory_metrics", "Capture::GetCapture");
  capture.time_ = os.GetMonotonic();
  zx_status_t err = os.GetInfo(state.root, ZX_INFO_KMEM_STATS, &capture.kmem_,
                               sizeof(capture.kmem_), nullptr, nullptr);
  if (err != ZX_OK) {
    return err;
  }

  if (level == KMEM) {
    return ZX_OK;
  }

  err = os.GetProcesses([&state, &capture, &os](int depth, zx_handle_t handle, zx_koid_t koid,
                                                zx_koid_t parent_koid) {
    if (koid == state.self_koid) {
      return ZX_OK;
    }

    Process process = {.koid = koid};
    zx_status_t s = os.GetProperty(handle, ZX_PROP_NAME, process.name, ZX_MAX_NAME_LEN);
    if (s != ZX_OK) {
      return s == ZX_ERR_BAD_STATE ? ZX_OK : s;
    }

    size_t num_vmos;
    s = os.GetInfo(handle, ZX_INFO_PROCESS_VMOS, nullptr, 0, nullptr, &num_vmos);
    if (s != ZX_OK) {
      return s == ZX_ERR_BAD_STATE ? ZX_OK : s;
    }
    auto vmos = std::make_unique<zx_info_vmo_t[]>(num_vmos);
    {
      s = os.GetInfo(handle, ZX_INFO_PROCESS_VMOS, vmos.get(), num_vmos * sizeof(zx_info_vmo_t),
                     &num_vmos, nullptr);
      if (s != ZX_OK) {
        return s == ZX_ERR_BAD_STATE ? ZX_OK : s;
      }
    }
    process.vmos.reserve(num_vmos);
    for (size_t i = 0; i < num_vmos; i++) {
      const auto& vmo = vmos[i];
      capture.koid_to_vmo_.try_emplace(vmo.koid, vmo);
      process.vmos.push_back(vmo.koid);
    }
    capture.koid_to_process_.emplace(koid, process);
    return ZX_OK;
  });
  return err;
}

}  // namespace memory
