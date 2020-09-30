// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_MIDI_SOURCE_H_
#define SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_MIDI_SOURCE_H_

#include <fuchsia/hardware/midi/llcpp/fidl.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/mutex.h>
#include <usb/request-cpp.h>
#include <usb/usb.h>

namespace audio {
namespace usb {

class UsbMidiSource;
using UsbMidiSourceBase = ddk::Device<UsbMidiSource, ddk::Unbindable, ddk::Openable,
                                      ddk::Closable, ddk::Readable, ddk::Messageable>;

class UsbMidiSource : public UsbMidiSourceBase,
                      public llcpp::fuchsia::hardware::midi::Device::Interface,
                      public ddk::EmptyProtocol<ZX_PROTOCOL_MIDI> {
 public:
  using UsbDevice = ::usb::UsbDevice;
  using UsbRequest = ::usb::Request<>;
  using UsbRequestQueue = ::usb::RequestQueue<>;

  UsbMidiSource(zx_device_t* parent, const UsbDevice& usb, size_t parent_req_size)
      : UsbMidiSourceBase(parent), usb_(usb), parent_req_size_(parent_req_size) {}

  static zx_status_t Create(zx_device_t* parent, const UsbDevice& usb, int index,
                            const usb_interface_descriptor_t* intf,
                            const usb_endpoint_descriptor_t* ep, size_t req_size);

  // Device protocol implementation.
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
  zx_status_t DdkClose(uint32_t flags);
  zx_status_t DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual);
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

  // FIDL methods.
  void GetInfo(GetInfoCompleter::Sync completer) final;

 private:
  zx_status_t Init(int index, const usb_interface_descriptor_t* intf,
                   const usb_endpoint_descriptor_t* ep);
  void UpdateSignals() TA_REQ(mutex_);
  void ReadComplete(usb_request_t* req);

  UsbDevice usb_;

  // pool of free USB requests
  UsbRequestQueue free_read_reqs_;
  // list of received packets not yet read by upper layer
  UsbRequestQueue completed_reads_;
  // mutex for synchronizing access to free_read_reqs, completed_reads and open
  fbl::Mutex mutex_;

  bool open_ TA_GUARDED(mutex_) = false;
  bool dead_ TA_GUARDED(mutex_) = false;

  // the last signals we reported
  zx_signals_t signals_ TA_GUARDED(mutex_);
  size_t parent_req_size_;
};

}  // namespace usb
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_MIDI_SOURCE_H_
