// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SIMPLEHID_SIMPLEHID_H_
#define LIB_SIMPLEHID_SIMPLEHID_H_

#include <lib/fit/function.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/port.h>
#include <threads.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/types.h>
#include <zircon/threads.h>

#include <ddk/debug.h>
#include <ddktl/protocol/hidbus.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

namespace simplehid {

// Helper class for a "simple" HID that only supports polling. This class implements HidbusStart and
// HidbusStop, and manages the hidbus IO queue for the user. Users pass in a callback to get input
// reports and forward calls to HidbusStart and HidbusStop to an instance of this class.
// GetReportInterval and SetReportInterval can be called by the user to get or set the polling
// interval.

template <typename InputReportType>
class SimpleHid {
 public:
  SimpleHid() : interval_ms_(0) {}

  SimpleHid(zx::port port, fit::function<zx_status_t(InputReportType*)> get_input_report)
      : port_(std::move(port)), interval_ms_(0), get_input_report_(std::move(get_input_report)) {}

  SimpleHid& operator=(SimpleHid&& other) {
    port_ = std::move(other.port_);
    get_input_report_ = std::move(other.get_input_report_);

    fbl::AutoLock lock1(&interval_lock_);
    fbl::AutoLock lock2(&other.interval_lock_);
    interval_ms_ = other.interval_ms_;
    return *this;
  }

  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc) {
    {
      fbl::AutoLock lock(&client_lock_);

      if (client_.is_valid()) {
        return ZX_ERR_ALREADY_BOUND;
      }

      client_ = ddk::HidbusIfcProtocolClient(ifc);
    }

    return thrd_status_to_zx_status(thrd_create_with_name(
        &thread_, [](void* arg) -> int { return reinterpret_cast<SimpleHid*>(arg)->Thread(); },
        this, "simplehid-thread"));
  }

  void HidbusStop() {
    zx_port_packet_t packet = {kPacketKeyStop, ZX_PKT_TYPE_USER, ZX_OK, {}};
    if (port_.queue(&packet) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to queue packet\n", __FILE__);
    }

    thrd_join(thread_, nullptr);

    fbl::AutoLock lock(&client_lock_);
    client_.clear();
  }

  zx_status_t SetReportInterval(uint32_t interval_ms) {
    {
      fbl::AutoLock lock(&interval_lock_);
      interval_ms_ = interval_ms;
    }

    zx_port_packet packet = {kPacketKeyConfigure, ZX_PKT_TYPE_USER, ZX_OK, {}};
    zx_status_t status = port_.queue(&packet);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to queue packet\n", __FILE__);
    }

    return status;
  }

  uint32_t GetReportInterval() {
    fbl::AutoLock lock(&interval_lock_);
    return interval_ms_;
  }

 private:
  enum PacketKeys {
    kPacketKeyPoll,
    kPacketKeyStop,
    kPacketKeyConfigure,
  };

  int Thread() {
    zx::time deadline = zx::time::infinite();

    while (1) {
      zx_port_packet_t packet;
      zx_status_t status = port_.wait(deadline, &packet);
      if (status != ZX_OK && status != ZX_ERR_TIMED_OUT) {
        return thrd_error;
      }

      if (status == ZX_ERR_TIMED_OUT) {
        packet.key = kPacketKeyPoll;
      }

      switch (packet.key) {
        case kPacketKeyStop:
          return thrd_success;

        case kPacketKeyPoll: {
          InputReportType report;
          if (get_input_report_(&report) == ZX_OK) {
            fbl::AutoLock lock(&client_lock_);
            if (client_.is_valid()) {
              client_.IoQueue(&report, sizeof(report), zx_clock_get_monotonic());
            }
          }

          __FALLTHROUGH;
        }

        case kPacketKeyConfigure:
          fbl::AutoLock lock(&interval_lock_);
          if (interval_ms_ == 0) {
            deadline = zx::time::infinite();
          } else {
            deadline = zx::deadline_after(zx::msec(interval_ms_));
          }
      }
    }

    return thrd_success;
  }

  fbl::Mutex client_lock_;
  fbl::Mutex interval_lock_;

  zx::port port_;
  ddk::HidbusIfcProtocolClient client_ TA_GUARDED(client_lock_);
  thrd_t thread_;
  uint32_t interval_ms_ TA_GUARDED(interval_lock_);

  fit::function<zx_status_t(InputReportType*)> get_input_report_;
};

}  // namespace simplehid

#endif  // LIB_SIMPLEHID_SIMPLEHID_H_
