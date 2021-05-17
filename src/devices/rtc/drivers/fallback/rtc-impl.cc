// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <librtc_llcpp.h>
#include <string.h>
#include <zircon/compiler.h>

#include <memory>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

#include "src/devices/rtc/drivers/fallback/fallback_rtc_bind.h"

namespace fallback_rtc {

class FallbackRtc;
using RtcDevice = ddk::Device<FallbackRtc, ddk::Messageable<fuchsia_hardware_rtc::Device>::Mixin>;

// The fallback RTC driver is a fake driver which avoids to special case
// in the upper layers on boards which don't have an RTC chip (and battery).
// it assumes that an external entity will set it to a approximately correct
// time based on other sources, most likely the roughtime service which
// runs at every boot.
class FallbackRtc : public RtcDevice,
                    public fidl::WireServer<fuchsia_hardware_rtc::Device>,
                    public ddk::EmptyProtocol<ZX_PROTOCOL_RTC> {
 public:
  FallbackRtc(zx_device_t* parent) : RtcDevice(parent), rtc_last_({}) {
    // We don't rely on the default value to be correct to any approximation
    // but for debugging purposes is best to return a known value.
    rtc_last_.year = 2018;
    rtc_last_.month = 1;
    rtc_last_.day = 1;
  }

  zx_status_t Bind() {
    // Check if inside an IsolatedDevmgr
    // TODO: Eventually we should figure out how drivers can be better isolated
    size_t size;
    zx_status_t status = DdkGetMetadataSize(DEVICE_METADATA_TEST, &size);
    if (status == ZX_OK && size == 1) {
      uint8_t metadata;
      status = DdkGetMetadata(DEVICE_METADATA_TEST, &metadata, 1, &size);
    }

    return DdkAdd("fallback-rtc");
  }

  void DdkRelease() { delete this; }

 private:
  void Get(GetRequestView request, GetCompleter::Sync& completer) {
    // TODO(cpu): Advance the clock. This is not strictly necessary at the
    // moment because this driver basically serves as a rendezvous between
    // a Internet time server and the rest of the system.

    completer.Reply(rtc_last_);
  }

  void Set(SetRequestView request, SetCompleter::Sync& completer) {
    if (!rtc::IsRtcValid(request->rtc)) {
      completer.Reply(ZX_ERR_OUT_OF_RANGE);
      return;
    }

    rtc_last_ = request->rtc;
    completer.Reply(ZX_OK);
  }

  fuchsia_hardware_rtc::wire::Time rtc_last_;
};

static zx_status_t fallback_rtc_bind(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<FallbackRtc>(parent);
  auto status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the device, until DdkRelease().
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

static constexpr zx_driver_ops_t ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = fallback_rtc_bind;
  return ops;
}();

}  // namespace fallback_rtc

ZIRCON_DRIVER(fallback_rtc, fallback_rtc::ops, "fallback_rtc", "0.1");
