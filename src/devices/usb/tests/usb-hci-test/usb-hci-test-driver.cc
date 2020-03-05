// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-hci-test-driver.h"

#include <lib/sync/completion.h>
#include <lib/zx/vmo.h>
#include <unistd.h>
#include <zircon/device/usb-peripheral.h>
#include <zircon/process.h>

#include <limits>
#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/usb.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <usb/request-cpp.h>

namespace usb {

void HciTest::Run(RunCompleter::Sync completer) {
  if (test_running_) {
    completer.ReplyError(ZX_ERR_CONNECTION_REFUSED);
    return;
  }
  test_thread_.emplace(
      [this, completion = completer.ToAsync()]() mutable { TestThread(std::move(completion)); });
}

void HciTest::TestThread(RunCompleter::Async completer) {
  test_running_ = true;
  fbl::AutoCall test_complete([this]() { test_running_ = false; });
  zx_status_t status = ZX_OK;
  llcpp::fuchsia::hardware::usb::hcitest::TestResults test_results;
  using Request = usb::CallbackRequest<sizeof(std::max_align_t) * 4>;
  size_t parent_size = usb_.GetRequestSize();
  bool running = true;
  uint64_t host_packets = 0;
  // Test recovery from CancelAll
  // TODO (fxb/33848): Add asserts on every CancelAll when the rewrite goes in.
  usb_.CancelAll(bulk_out_.bEndpointAddress);
  struct {
    uint64_t start;
    uint64_t end;
    uint64_t bulk_in_packets;
    uint64_t bulk_out_packets;
  } results;
  enum UsbTesterCommand {
    StartTransfers = 0xE2,
    StopTransfers = 0xE3,
    StartShortPacketTests = 0xE4,
  };
  size_t actual;
  status = usb_.ControlOut(USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE, StartShortPacketTests,
                           0, 0, ZX_TIME_INFINITE, nullptr, 0);
  bool correct_byte_count = true;
  if (status == ZX_OK) {
    {
      std::optional<Request> request;
      sync_completion_t completion;
      size_t bytes;
      Request::Alloc(&request, 4096 * 3, bulk_in_.bEndpointAddress, parent_size,
                     [&bytes, &completion](Request request) {
                       bytes = request.request()->response.actual;
                       sync_completion_signal(&completion);
                     });
      request->Queue(usb_);
      sync_completion_wait(&completion, ZX_TIME_INFINITE);
      // First packet
      correct_byte_count &= bytes == 20;
    }
    {
      std::optional<Request> request;
      sync_completion_t completion;
      size_t bytes;
      Request::Alloc(&request, 4096 * 3, bulk_in_.bEndpointAddress, parent_size,
                     [&bytes, &completion](Request request) {
                       bytes = request.request()->response.actual;
                       sync_completion_signal(&completion);
                     });
      request->Queue(usb_);
      sync_completion_wait(&completion, ZX_TIME_INFINITE);
      // Middle packet
      correct_byte_count &= bytes == 4098;
    }
    {
      std::optional<Request> request;
      sync_completion_t completion;
      size_t bytes;
      Request::Alloc(&request, 4096 * 3, bulk_in_.bEndpointAddress, parent_size,
                     [&bytes, &completion](Request request) {
                       bytes = request.request()->response.actual;
                       sync_completion_signal(&completion);
                     });
      request->Queue(usb_);
      sync_completion_wait(&completion, ZX_TIME_INFINITE);
      // Last packet
      correct_byte_count &= bytes == (4096 * 2) + 512;
    }
    status = usb_.ControlIn(USB_TYPE_VENDOR | USB_DIR_IN | USB_RECIP_DEVICE, StopTransfers, 0, 0,
                            ZX_TIME_INFINITE, &results, sizeof(results), &actual);
  } else {
    usb_.ResetEndpoint(0);
  }

  for (size_t i = 0; i < 4096; i++) {
    std::optional<Request> request;
    Request::Alloc(&request, 8192, bulk_out_.bEndpointAddress, parent_size,
                   [&running, this, &host_packets](Request request) {
                     if (!running) {
                       return;
                     }
                     Request::Queue(std::move(request), usb_);
                     host_packets++;
                   });
    request->request()->direct = true;
    request->request()->header.length = usb_ep_max_packet(&bulk_out_);
    Request::Queue(std::move(*request), usb_);
  }
  // E2 start, E3 stop
  usb_.ControlOut(USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE, StartTransfers, 0, 0,
                  ZX_TIME_INFINITE, nullptr, 0);
  constexpr auto kTestRuntime = 15;
  sleep(kTestRuntime);
  running = false;
  usb_.CancelAll(bulk_out_.bEndpointAddress);
  // Test the case where we haven't queued any data
  usb_.CancelAll(bulk_out_.bEndpointAddress);
  status = usb_.ControlIn(USB_TYPE_VENDOR | USB_DIR_IN | USB_RECIP_DEVICE, StopTransfers, 0, 0,
                          ZX_TIME_INFINITE, &results, sizeof(results), &actual);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  test_results.received_bulk_packets = host_packets;
  uint64_t clock_val = 0;
  uint64_t dropped_packets = 0;
  uint64_t isoch_packets = 0;
  // Timestamp in 125 microsecond intervals
  // TODO (fxb/34507): Run isochronous test under load (at the same time as bulk)
  // once we get scheduling issues fixed.
  uint64_t timestamp = (usb_.GetCurrentFrame() + 20) * 8;
  running = true;
  // 8 transfers per millisecond (at rate of 125 microseconds per transfer)
  // We batch 5 1 millisecond transfers at a time.
  for (size_t i = 0; i < 8 * 5; i++) {
    std::optional<Request> request;
    Request::Alloc(&request, isoch_in_.wMaxPacketSize, isoch_in_.bEndpointAddress, parent_size,
                   [&isoch_packets, &clock_val, &dropped_packets, &timestamp, &running,
                    this](Request request) {
                     if (!running) {
                       return;
                     }
                     isoch_packets++;
                     if (clock_val == 0) {
                       request.CopyFrom(&clock_val, sizeof(clock_val), 0);
                     } else {
                       uint64_t device_val;
                       request.CopyFrom(&device_val, sizeof(device_val), 0);
                       if (clock_val > device_val) {
                         return;
                       }
                       if (clock_val + 1 != device_val) {
                         dropped_packets = device_val - clock_val;
                       }
                       clock_val = device_val;
                     }
                     request.request()->header.frame = timestamp / 8;
                     timestamp++;
                     Request::Queue(std::move(request), usb_);
                   });
    (*request).request()->header.frame = timestamp / 8;
    timestamp++;
    request->request()->direct = true;
    Request::Queue(std::move(*request), usb_);
  }
  sleep(kTestRuntime);
  test_results.received_isoch_packets = isoch_packets;
  test_results.isoch_packet_size = isoch_in_.wMaxPacketSize;
  test_results.bulk_packet_size = bulk_out_.wMaxPacketSize;
  test_results.got_correct_number_of_bytes_in_short_transfers = correct_byte_count;
  running = false;
  usb_.CancelAll(isoch_in_.bEndpointAddress);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess(test_results);
}

zx_status_t HciTest::Bind() { return DdkAdd("usb-hci-test"); }

void HciTest::EnumerationThread(ddk::InitTxn txn) {
  if (!usb_.is_valid()) {
    txn.Reply(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  constexpr auto kNumEndpoints = 5;
  constexpr auto kInterfaceSubClass = 0;
  constexpr auto kInterfaceProtocol = 0;

  enum UsbInterface {
    InterruptIn = 0,
    IsochIn = 1,
    BulkOut = 3,
    BulkIn = 4,
  };

  std::optional<usb::InterfaceList> interfaces;
  usb::InterfaceList::Create(usb_, true, &interfaces);
  bool configured = false;
  for (auto iface : *interfaces) {
    if ((iface.descriptor()->bNumEndpoints == kNumEndpoints) &&
        (iface.descriptor()->bInterfaceSubClass == kInterfaceSubClass) &&
        (iface.descriptor()->bInterfaceProtocol == kInterfaceProtocol)) {
      usb_.SetInterface(iface.descriptor()->bInterfaceNumber,
                        iface.descriptor()->bAlternateSetting);
      size_t i = 0;
      for (auto ep : iface.GetEndpointList()) {
        switch (i) {
          case InterruptIn:
            // Interrupt endpoint
            irq_in_ = ep.descriptor;
            irq_in_3_.bDescriptorType = 0;
            if (ep.has_companion) {
              irq_in_3_ = ep.ss_companion;
              usb_.EnableEndpoint(&ep.descriptor, &irq_in_3_, true);
            } else {
              usb_.EnableEndpoint(&ep.descriptor, nullptr, true);
            }
            break;
          case IsochIn:
            isoch_in_ = ep.descriptor;
            isoch_in_3_.bDescriptorType = 0;
            if (ep.has_companion) {
              isoch_in_3_ = ep.ss_companion;
              usb_.EnableEndpoint(&ep.descriptor, &isoch_in_3_, true);
            } else {
              usb_.EnableEndpoint(&ep.descriptor, nullptr, true);
            }
            break;
          case BulkOut:
            bulk_out_ = ep.descriptor;
            bulk_out_3_.bDescriptorType = 0;
            if (ep.has_companion) {
              bulk_out_3_ = ep.ss_companion;
              usb_.EnableEndpoint(&ep.descriptor, &bulk_out_3_, true);
            } else {
              usb_.EnableEndpoint(&ep.descriptor, nullptr, true);
            }
            break;
          case BulkIn:
            configured = true;
            bulk_in_ = ep.descriptor;
            bulk_in_3_.bDescriptorType = 0;
            if (ep.has_companion) {
              bulk_in_3_ = ep.ss_companion;
              usb_.EnableEndpoint(&ep.descriptor, &bulk_in_3_, true);
            } else {
              usb_.EnableEndpoint(&ep.descriptor, nullptr, true);
            }
            break;
        }
        i++;
      }
    }
  }
  if (!configured) {
    txn.Reply(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  txn.Reply(ZX_OK);
}

void HciTest::DdkInit(ddk::InitTxn txn) {
  enumeration_thread_.emplace([this, transaction = std::move(txn)]() mutable {
    EnumerationThread(std::move(transaction));
  });
}

zx_status_t HciTest::Create(void* ctx, zx_device_t* parent) {
  ddk::UsbProtocolClient usb(parent);
  auto dev = std::make_unique<HciTest>(parent, usb);

  zx_status_t status = dev->Bind();
  if (status == ZX_OK) {
    // Intentionally leak as it is now held by DevMgr.
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

static constexpr zx_driver_ops_t kHciTestDriverOps = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = usb::HciTest::Create;
  return ops;
}();

}  // namespace usb

// clang-format off
ZIRCON_DRIVER_BEGIN(usb_HciTest, usb::kHciTestDriverOps, "zircon", "0.1", 6)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_VID, GOOGLE_USB_VID),
    BI_ABORT_IF(NE, BIND_USB_PID, GOOGLE_USB_FUNCTION_TEST_PID),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, 0),
ZIRCON_DRIVER_END(usb_HciTest)
    // clang-format on
