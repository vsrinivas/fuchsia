// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-midi-source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddktl/fidl.h>
#include <fbl/auto_lock.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

#include "midi.h"
#include "usb-audio.h"

namespace audio {
namespace usb {

constexpr size_t READ_REQ_COUNT = 20;

void UsbMidiSource::UpdateSignals() {
  zx_signals_t new_signals = 0;
  if (dead_) {
    new_signals |= (DEV_STATE_READABLE | DEV_STATE_ERROR);
  } else if (!completed_reads_.is_empty()) {
    new_signals |= DEV_STATE_READABLE;
  }
  if (new_signals != signals_) {
    ClearAndSetState(signals_ & ~new_signals, new_signals & ~signals_);
    signals_ = new_signals;
  }
}

void UsbMidiSource::ReadComplete(usb_request_t* req) {
  if (req->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(req);
    return;
  }

  fbl::AutoLock lock(&mutex_);

  if (req->response.status == ZX_OK && req->response.actual > 0) {
    completed_reads_.push(UsbRequest(req, parent_req_size_));
  } else {
    usb_request_complete_t complete = {
        .callback = [](void* ctx,
                       usb_request_t* req) { static_cast<UsbMidiSource*>(ctx)->ReadComplete(req); },
        .ctx = this,
    };
    usb_.RequestQueue(req, &complete);
  }

  UpdateSignals();
}

void UsbMidiSource::DdkUnbind(ddk::UnbindTxn txn) {
  fbl::AutoLock al(&mutex_);
  dead_ = true;

  UpdateSignals();
  txn.Reply();
}

void UsbMidiSource::DdkRelease() { delete this; }

zx_status_t UsbMidiSource::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
  zx_status_t result;

  fbl::AutoLock lock(&mutex_);
  if (open_) {
    result = ZX_ERR_ALREADY_BOUND;
  } else {
    open_ = true;
    result = ZX_OK;
  }

  // queue up reads, including stale completed reads
  usb_request_complete_t complete = {
      .callback = [](void* ctx,
                     usb_request_t* req) { static_cast<UsbMidiSource*>(ctx)->ReadComplete(req); },
      .ctx = this,
  };
  std::optional<UsbRequest> req;
  while ((req = completed_reads_.pop()).has_value()) {
    usb_.RequestQueue(req->take(), &complete);
  }
  while ((req = free_read_reqs_.pop()).has_value()) {
    usb_.RequestQueue(req->take(), &complete);
  }

  return result;
}

zx_status_t UsbMidiSource::DdkClose(uint32_t flags) {
  fbl::AutoLock lock(&mutex_);
  open_ = false;

  return ZX_OK;
}

zx_status_t UsbMidiSource::DdkRead(void* data, size_t len, zx_off_t off, size_t* actual) {
  {
    fbl::AutoLock al(&mutex_);
    if (dead_) {
      return ZX_ERR_IO_NOT_PRESENT;
    }
  }

  zx_status_t status = ZX_OK;
  if (len < 3) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  fbl::AutoLock lock(&mutex_);

  auto req = completed_reads_.pop();
  if (!req.has_value()) {
    UpdateSignals();
    return ZX_ERR_SHOULD_WAIT;
  }

  // MIDI events are 4 bytes. We can ignore the zeroth byte
  req->CopyFrom(data, 3, 1);
  *actual = get_midi_message_length(*(static_cast<uint8_t*>(data)));
  free_read_reqs_.push(std::move(*req));

  usb_request_complete_t complete = {
      .callback = [](void* ctx,
                     usb_request_t* req) { static_cast<UsbMidiSource*>(ctx)->ReadComplete(req); },
      .ctx = this,
  };

  while ((req = free_read_reqs_.pop()).has_value()) {
    usb_.RequestQueue(req->take(), &complete);
  }

  UpdateSignals();
  return status;
}

void UsbMidiSource::GetInfo(GetInfoCompleter::Sync completer) {
  llcpp::fuchsia::hardware::midi::Info info = {
      .is_sink = false,
      .is_source = true,
  };
  completer.Reply(info);
}

zx_status_t UsbMidiSource::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::midi::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

zx_status_t UsbMidiSource::Create(zx_device_t* parent, const UsbDevice& usb, int index,
                                  const usb_interface_descriptor_t* intf,
                                  const usb_endpoint_descriptor_t* ep, const size_t req_size) {
  auto dev = std::make_unique<UsbMidiSource>(parent, usb, req_size);
  auto status = dev->Init(index, intf, ep);
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  [[maybe_unused]] auto* _ = dev.release();
  return ZX_OK;
}

zx_status_t UsbMidiSource::Init(int index, const usb_interface_descriptor_t* intf,
                                const usb_endpoint_descriptor_t* ep) {
  int packet_size = usb_ep_max_packet(ep);
  if (intf->bAlternateSetting != 0) {
    usb_.SetInterface(intf->bInterfaceNumber, intf->bAlternateSetting);
  }
  for (size_t i = 0; i < READ_REQ_COUNT; i++) {
    std::optional<UsbRequest> req;
    auto status = UsbRequest::Alloc(&req, packet_size, ep->bEndpointAddress, parent_req_size_);
    if (status != ZX_OK) {
      return status;
    }
    req->request()->header.length = packet_size;
    free_read_reqs_.push(std::move(*req));
  }

  char name[ZX_DEVICE_NAME_MAX];
  snprintf(name, sizeof(name), "usb-midi-source-%d", index);

  return DdkAdd(name);
}

}  // namespace usb
}  // namespace audio
