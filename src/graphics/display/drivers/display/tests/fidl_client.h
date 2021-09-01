// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_TESTS_FIDL_CLIENT_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_TESTS_FIDL_CLIENT_H_

#include <fidl/fuchsia.hardware.display/cpp/wire.h>
#include <lib/fidl/cpp/message.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "base.h"

namespace display {

class TestFidlClient {
 public:
  class Display {
   public:
    Display(const fuchsia_hardware_display::wire::Info& info);

    uint64_t id_;
    fbl::Vector<zx_pixel_format_t> pixel_formats_;
    fbl::Vector<fuchsia_hardware_display::wire::Mode> modes_;
    fbl::Vector<fuchsia_hardware_display::wire::CursorInfo> cursors_;

    fbl::String manufacturer_name_;
    fbl::String monitor_name_;
    fbl::String monitor_serial_;

    fuchsia_hardware_display::wire::ImageConfig image_config_;
  };

  TestFidlClient(fidl::WireSyncClient<fuchsia_sysmem::Allocator>* sysmem) : sysmem_(sysmem) {}
  ~TestFidlClient();

  bool CreateChannel(zx_handle_t provider, bool is_vc);
  // Enable vsync for a display and wait for events using |dispatcher|.
  bool Bind(async_dispatcher_t* dispatcher) TA_EXCL(mtx());
  zx_status_t ImportImageWithSysmem(const fuchsia_hardware_display::wire::ImageConfig& image_config,
                                    uint64_t* image_id) TA_EXCL(mtx());
  zx_status_t PresentImage();
  uint64_t display_id() const;

  fbl::Vector<Display> displays_;
  std::unique_ptr<fidl::WireSyncClient<fuchsia_hardware_display::Controller>> dc_ TA_GUARDED(mtx());
  fidl::WireSyncClient<fuchsia_sysmem::Allocator>* sysmem_;
  zx::handle device_handle_;
  bool has_ownership_ = false;
  uint64_t image_id_ = 0;
  uint64_t layer_id_ = 0;

  uint64_t vsync_count() const {
    fbl::AutoLock lock(mtx());
    return vsync_count_;
  }

  const std::vector<uint64_t>& recent_vsync_images() const { return recent_vsync_images_; }

  uint64_t get_cookie() const { return cookie_; }

  fbl::Mutex* mtx() const { return &mtx_; }

 private:
  mutable fbl::Mutex mtx_;
  async_dispatcher_t* dispatcher_ = nullptr;
  uint64_t vsync_count_ TA_GUARDED(mtx()) = 0;
  uint64_t cookie_ = 0;
  std::vector<uint64_t> recent_vsync_images_;

  zx_status_t ImportImageWithSysmemLocked(
      const fuchsia_hardware_display::wire::ImageConfig& image_config, uint64_t* image_id)
      TA_REQ(mtx());
  void OnEventMsgAsync(async_dispatcher_t* dispatcher, async::WaitBase* self, zx_status_t status,
                       const zx_packet_signal_t* signal) TA_EXCL(mtx());
  async::WaitMethod<TestFidlClient, &TestFidlClient::OnEventMsgAsync> wait_events_{this};
};

}  // namespace display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_TESTS_FIDL_CLIENT_H_
