// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/usb.h>
#include <ddktl/device.h>
#include <fbl/array.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <zircon/thread_annotations.h>

#include "usb-audio-control-interface.h"
#include "usb-audio-descriptors.h"
#include "debug-logging.h"

namespace audio {
namespace usb {

class UsbAudioStream;

class UsbAudioDevice;
using UsbAudioDeviceBase = ddk::Device<UsbAudioDevice, ddk::Unbindable>;

class UsbAudioDevice : public UsbAudioDeviceBase,
                       public fbl::RefCounted<UsbAudioDevice> {
public:
    static zx_status_t DriverBind(zx_device_t* parent);
    void DdkUnbind();
    void DdkRelease();

    void RemoveAudioStream(const fbl::RefPtr<UsbAudioStream>& stream);

    const char* log_prefix() const { return log_prefix_; }
    const usb_device_descriptor_t& desc() const { return usb_dev_desc_; }
    const fbl::RefPtr<DescriptorListMemory>& desc_list() const { return desc_list_; }
    const usb_protocol_t& usb_proto() const { return usb_proto_; }
    uint16_t vid() const { return usb_dev_desc_.idVendor; }
    uint16_t pid() const { return usb_dev_desc_.idProduct; }
    const fbl::Array<uint8_t>& mfr_name() const { return mfr_name_; }
    const fbl::Array<uint8_t>& prod_name() const { return prod_name_; }
    const fbl::Array<uint8_t>& serial_num() const { return serial_num_; }

private:
    explicit UsbAudioDevice(zx_device_t* parent);

    // A small struct used when searching descriptors for midi streaming
    // interfaces.
    //
    // TODO(johngro) : Someday, turn this into something more like
    // UsbAudioStreamingInterface and give it the ability to parse and
    // understand its class specific interfaces, class specific endpoints, and
    // manage multiple alternate interface settings.
    struct MidiStreamingInfo {
        explicit MidiStreamingInfo(const usb_interface_descriptor_t* i) : ifc(i) {}
        const usb_interface_descriptor_t* ifc;
        const usb_endpoint_descriptor_t*  in_ep  = nullptr;
        const usb_endpoint_descriptor_t*  out_ep = nullptr;
    };

    zx_status_t Bind();
    void Probe();
    void ParseMidiStreamingIfc(DescriptorListMemory::Iterator* iter,
                               MidiStreamingInfo* inout_info);

    char log_prefix_[LOG_PREFIX_STORAGE] = { 0 };

    usb_protocol_t usb_proto_;
    fbl::Mutex lock_;
    usb_device_descriptor_t usb_dev_desc_;
    fbl::Array<uint8_t> mfr_name_;
    fbl::Array<uint8_t> prod_name_;
    fbl::Array<uint8_t> serial_num_;
    fbl::RefPtr<DescriptorListMemory> desc_list_;
    fbl::DoublyLinkedList<fbl::RefPtr<UsbAudioStream>> streams_ TA_GUARDED(lock_);

    int midi_sink_index_ = 0;
    int midi_source_index_ = 0;
};

}  // namespace usb
}  // namespace audio
