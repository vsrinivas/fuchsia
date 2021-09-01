// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_MIDI_SOURCE_H_
#define SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_MIDI_SOURCE_H_

#include <fidl/fuchsia.hardware.midi/cpp/wire.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <usb/request-cpp.h>
#include <usb/usb.h>

namespace audio {
namespace usb {

class UsbMidiSource;
using UsbMidiSourceBase = ddk::Device<UsbMidiSource, ddk::Unbindable, ddk::Openable, ddk::Closable,
                                      ddk::Messageable<fuchsia_hardware_midi::Device>::Mixin>;

class UsbMidiSource : public UsbMidiSourceBase, public ddk::EmptyProtocol<ZX_PROTOCOL_MIDI> {
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

  // FIDL methods.
  void GetInfo(GetInfoRequestView request, GetInfoCompleter::Sync& completer) final;
  void Read(ReadRequestView request, ReadCompleter::Sync& completer) final;
  void Write(WriteRequestView request, WriteCompleter::Sync& completer) final;

 private:
  zx_status_t Init(int index, const usb_interface_descriptor_t* intf,
                   const usb_endpoint_descriptor_t* ep);
  void ReadComplete(usb_request_t* req);

  zx_status_t ReadInternal(void* data, size_t len, size_t* actual);

  UsbDevice usb_;

  // mutex for synchronizing access to free_read_reqs, completed_reads and open
  fbl::Mutex mutex_;

  // pool of free USB requests
  UsbRequestQueue free_read_reqs_ TA_GUARDED(mutex_);
  // list of received packets not yet read by upper layer
  UsbRequestQueue completed_reads_ TA_GUARDED(mutex_);

  bool open_ TA_GUARDED(mutex_) = false;
  bool dead_ TA_GUARDED(mutex_) = false;

  // Signals when completed_reads_ is non-empty.
  fbl::ConditionVariable read_ready_ TA_GUARDED(mutex_);

  // the last signals we reported
  size_t parent_req_size_;
};

}  // namespace usb
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_MIDI_SOURCE_H_
