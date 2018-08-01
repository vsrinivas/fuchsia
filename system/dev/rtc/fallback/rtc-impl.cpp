// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <string.h>

#include <ddk/debug.h>

#include <ddktl/device.h>
#include <ddktl/protocol/rtc.h>

#include <fbl/unique_ptr.h>

#include <zircon/compiler.h>
#include <zircon/device/rtc.h>

#include <librtc.h>

namespace {

class FallbackRtc;
using RtcDevice = ddk::Device<FallbackRtc, ddk::Ioctlable>;

// The fallback RTC driver is a fake driver which avoids to special case
// in the upper layers on boards which don't have an RTC chip (and battery).
// it assumes that an external entity will set it to a approximately correct
// time based on other sources, most likely the roughtime service which
// runs at every boot.
class FallbackRtc : public RtcDevice,
                    public ddk::RtcProtocol<FallbackRtc> {
  public:
    FallbackRtc(zx_device_t* parent)
        : RtcDevice(parent), rtc_last_({}) {
        // We don't rely on the default value to be correct to any approximation
        // but for debugging purposes is best to return a known value.
        rtc_last_.year = 2018;
        rtc_last_.month = 1;
        rtc_last_.day = 1;
    }

    zx_status_t Bind() {
        return DdkAdd("fallback-rtc");
    }

    void DdkRelease() {
        delete this;
    }

    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len, size_t* out_actual) {
        switch (op) {
        case IOCTL_RTC_GET: return Get(out_buf, out_len, out_actual);
        case IOCTL_RTC_SET: return Set(in_buf, in_len);
        default: return ZX_ERR_NOT_SUPPORTED;
        }
    }

  private:
    zx_status_t Get(void* out_buf, size_t out_len, size_t* out_actual) {
        if (out_len < sizeof(rtc_last_)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }

        // TODO(cpu): Advance the clock. This is not strictly necessary at the
        // moment because this driver basically serves as a rendezvous between
        // a Internet time server and the rest of the system.

        memcpy(out_buf, &rtc_last_, sizeof(rtc_last_));
        return ZX_OK;
    }

    zx_status_t Set(const void* in_buf, size_t in_len) {
        if (in_len < sizeof(rtc_last_)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }

        auto rtc_new = *reinterpret_cast<const rtc_t*>(in_buf);
        if (rtc_is_invalid(&rtc_new)) {
            return ZX_ERR_OUT_OF_RANGE;
        }

        rtc_last_ = rtc_new;

        zx_status_t status = set_utc_offset(&rtc_last_);
        if (status != ZX_OK) {
            zxlogf(ERROR, "The RTC driver was unable to set the UTC clock!\n");
        }

        return ZX_OK;
    }

    rtc_t rtc_last_;
};

}  // namespace

extern "C" zx_status_t fallback_rtc_bind(void* ctx, zx_device_t* parent) {
    auto dev = fbl::make_unique<FallbackRtc>(parent);
    auto status = dev->Bind();
    if (status == ZX_OK) {
        // devmgr is now in charge of the device, until DdkRelease().
        __UNUSED auto ptr = dev.release();
    }
    return status;
}
