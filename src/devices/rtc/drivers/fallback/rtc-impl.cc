// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <librtc.h>
#include <string.h>
#include <zircon/compiler.h>

#include <memory>

#include <lib/ddk/metadata.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

#include "src/devices/rtc/drivers/fallback/fallback_rtc_bind.h"

namespace fallback_rtc {

static zx_status_t fidl_Get(void* ctx, fidl_txn_t* txn);
static zx_status_t fidl_Set(void* ctx, const fuchsia_hardware_rtc_Time* rtc, fidl_txn_t* txn);

class FallbackRtc;
using RtcDevice = ddk::Device<FallbackRtc, ddk::Messageable>;

// The fallback RTC driver is a fake driver which avoids to special case
// in the upper layers on boards which don't have an RTC chip (and battery).
// it assumes that an external entity will set it to a approximately correct
// time based on other sources, most likely the roughtime service which
// runs at every boot.
class FallbackRtc : public RtcDevice, public ddk::EmptyProtocol<ZX_PROTOCOL_RTC> {
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

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_hardware_rtc_Device_dispatch(this, txn, msg, &fidl_ops_);
  }

 private:
  zx_status_t Get(fuchsia_hardware_rtc_Time& rtc) {
    // TODO(cpu): Advance the clock. This is not strictly necessary at the
    // moment because this driver basically serves as a rendezvous between
    // a Internet time server and the rest of the system.

    rtc = rtc_last_;
    return ZX_OK;
  }

  zx_status_t Set(const fuchsia_hardware_rtc_Time& rtc) {
    if (rtc_is_invalid(&rtc)) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    rtc_last_ = rtc;
    return ZX_OK;
  }

  friend zx_status_t fidl_Get(void*, fidl_txn_t*);
  friend zx_status_t fidl_Set(void*, const fuchsia_hardware_rtc_Time*, fidl_txn_t*);

  const fuchsia_hardware_rtc_Device_ops_t fidl_ops_ = {
      .Get = fidl_Get,
      .Set = fidl_Set,
  };

  fuchsia_hardware_rtc_Time rtc_last_;
};

zx_status_t fidl_Get(void* ctx, fidl_txn_t* txn) {
  auto dev = static_cast<FallbackRtc*>(ctx);
  fuchsia_hardware_rtc_Time rtc;
  dev->Get(rtc);
  return fuchsia_hardware_rtc_DeviceGet_reply(txn, &rtc);
}

zx_status_t fidl_Set(void* ctx, const fuchsia_hardware_rtc_Time* rtc, fidl_txn_t* txn) {
  auto dev = static_cast<FallbackRtc*>(ctx);
  auto status = dev->Set(*rtc);
  return fuchsia_hardware_rtc_DeviceSet_reply(txn, status);
}

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
