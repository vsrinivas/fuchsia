// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-midi-sink.h"

#include <lib/sync/completion.h>
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

constexpr size_t WRITE_REQ_COUNT = 20;

namespace audio {
namespace usb {

void UsbMidiSink::UpdateSignals() {
  zx_signals_t new_signals = 0;
  if (dead_) {
    new_signals |= (DEV_STATE_WRITABLE | DEV_STATE_ERROR);
  } else if (!free_write_reqs_.is_empty()) {
    new_signals |= DEV_STATE_WRITABLE;
  }
  if (new_signals != signals_) {
    ClearAndSetState(signals_ & ~new_signals, new_signals & ~signals_);
    signals_ = new_signals;
  }
}

void UsbMidiSink::WriteComplete(usb_request_t* req) {
  if (req->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(req);
    return;
  }

  fbl::AutoLock lock(&mutex_);
  free_write_reqs_.push(UsbRequest(req, parent_req_size_));
  sync_completion_signal(&free_write_completion_);
  UpdateSignals();
}

void UsbMidiSink::DdkUnbind(ddk::UnbindTxn txn) {
  fbl::AutoLock al(&mutex_);
  dead_ = true;

  UpdateSignals();
  sync_completion_signal(&free_write_completion_);
  txn.Reply();
}

void UsbMidiSink::DdkRelease() { delete this; }

zx_status_t UsbMidiSink::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
  zx_status_t result;

  fbl::AutoLock lock(&mutex_);
  if (open_) {
    result = ZX_ERR_ALREADY_BOUND;
  } else {
    open_ = true;
    result = ZX_OK;
  }

  return result;
}

zx_status_t UsbMidiSink::DdkClose(uint32_t flags) {
  fbl::AutoLock lock(&mutex_);
  open_ = false;

  return ZX_OK;
}

zx_status_t UsbMidiSink::DdkWrite(const void* data, size_t length, zx_off_t offset,
                                  size_t* actual) {
  {
    fbl::AutoLock al(&mutex_);
    if (dead_) {
      return ZX_ERR_IO_NOT_PRESENT;
    }
  }

  zx_status_t status = ZX_OK;
  size_t out_actual = length;

  auto src = static_cast<const uint8_t*>(data);

  while (length > 0) {
    sync_completion_wait(&free_write_completion_, ZX_TIME_INFINITE);
    {
      fbl::AutoLock al(&mutex_);
      if (dead_) {
        return ZX_ERR_IO_NOT_PRESENT;
      }
    }

    std::optional<UsbRequest> req;
    {
      fbl::AutoLock lock(&mutex_);
      req = free_write_reqs_.pop();
      if (free_write_reqs_.is_empty()) {
        sync_completion_reset(&free_write_completion_);
      }
    }

    if (!req) {
      // shouldn't happen!
      fbl::AutoLock al(&mutex_);
      UpdateSignals();
      return ZX_ERR_INTERNAL;
    }

    size_t message_length = get_midi_message_length(*src);
    if (message_length < 1 || message_length > length)
      return ZX_ERR_INVALID_ARGS;

    uint8_t buffer[4];
    buffer[0] = (src[0] & 0xF0) >> 4;
    buffer[1] = src[0];
    buffer[2] = (message_length > 1 ? src[1] : 0);
    buffer[3] = (message_length > 2 ? src[2] : 0);

    req->CopyTo(buffer, 4, 0);
    req->request()->header.length = 4;
    usb_request_complete_t complete = {
        .callback = [](void* ctx,
                       usb_request_t* req) { static_cast<UsbMidiSink*>(ctx)->WriteComplete(req); },
        .ctx = this,
    };
    usb_.RequestQueue(req->take(), &complete);

    src += message_length;
    length -= message_length;
  }

  fbl::AutoLock al(&mutex_);
  UpdateSignals();
  if (status == ZX_OK) {
    *actual = out_actual;
  }
  return status;
}

void UsbMidiSink::GetInfo(GetInfoCompleter::Sync completer) {
  llcpp::fuchsia::hardware::midi::Info info = {
      .is_sink = true,
      .is_source = false,
  };
  completer.Reply(info);
}

zx_status_t UsbMidiSink::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::midi::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

zx_status_t UsbMidiSink::Create(zx_device_t* parent, const UsbDevice& usb, int index,
                                const usb_interface_descriptor_t* intf,
                                const usb_endpoint_descriptor_t* ep, const size_t req_size) {
  auto dev = std::make_unique<UsbMidiSink>(parent, usb, req_size);
  auto status = dev->Init(index, intf, ep);
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  [[maybe_unused]] auto* _ = dev.release();
  return ZX_OK;
}

zx_status_t UsbMidiSink::Init(int index, const usb_interface_descriptor_t* intf,
                              const usb_endpoint_descriptor_t* ep) {
  int packet_size = usb_ep_max_packet(ep);
  if (intf->bAlternateSetting != 0) {
    usb_.SetInterface(intf->bInterfaceNumber, intf->bAlternateSetting);
  }

  for (size_t i = 0; i < WRITE_REQ_COUNT; i++) {
    std::optional<UsbRequest> req;
    auto status =
        UsbRequest::Alloc(&req, usb_ep_max_packet(ep), ep->bEndpointAddress, parent_req_size_);
    if (status != ZX_OK) {
      return status;
    }
    req->request()->header.length = packet_size;
    free_write_reqs_.push(std::move(*req));
  }
  sync_completion_signal(&free_write_completion_);

  char name[ZX_DEVICE_NAME_MAX];
  snprintf(name, sizeof(name), "usb-midi-sink-%d", index);

  return DdkAdd(name);
}

}  // namespace usb
}  // namespace audio
