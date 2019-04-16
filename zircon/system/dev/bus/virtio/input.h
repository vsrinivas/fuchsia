// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <stdlib.h>

#include <ddk/io-buffer.h>
#include <ddk/protocol/hidbus.h>
#include <fuchsia/hardware/pty/c/fidl.h>
#include <hid/boot.h>
#include <virtio/input.h>

#include "device.h"
#include "input_kbd.h"
#include "input_touch.h"
#include "ring.h"

namespace virtio {

class InputDevice : public Device {
public:
    InputDevice(zx_device_t* device, zx::bti bti, fbl::unique_ptr<Backend> backend);
    virtual ~InputDevice();

    zx_status_t Init() override;
    void Release() override;

    void IrqRingUpdate() override;
    void IrqConfigChange() override;
    const char* tag() const override { return "virtio-input"; }

private:
    // DDK driver hooks
    static zx_status_t virtio_input_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn);
    static void virtio_input_release(void* ctx);

    static zx_status_t virtio_input_query(void* ctx, uint32_t options, hid_info_t* info);
    static zx_status_t virtio_input_start(void* ctx, const hidbus_ifc_protocol_t* ifc);
    static void virtio_input_stop(void* ctx);
    static zx_status_t virtio_input_get_descriptor(void* ctx, uint8_t desc_type,
                                                   void** data, size_t* len);
    static zx_status_t virtio_input_get_report(void* ctx, uint8_t rpt_type, uint8_t rpt_id,
                                               void* data, size_t len, size_t* out_len);
    static zx_status_t virtio_input_set_report(void* ctx, uint8_t rpt_type, uint8_t rpt_id,
                                               const void* data, size_t len);
    static zx_status_t virtio_input_get_idle(void* ctx, uint8_t rpt_type, uint8_t* duration);
    static zx_status_t virtio_input_set_idle(void* ctx, uint8_t rpt_type, uint8_t duration);
    static zx_status_t virtio_input_get_protocol(void* ctx, uint8_t* protocol);
    static zx_status_t virtio_input_set_protocol(void* ctx, uint8_t protocol);

    zx_status_t Start(const hidbus_ifc_protocol_t* ifc);
    void Stop();
    zx_status_t Query(uint32_t options, hid_info_t* info);
    zx_status_t GetDescriptor(uint8_t desc_type, void** data, size_t* len);
    void ReceiveEvent(virtio_input_event_t* event);

    void SelectConfig(uint8_t select, uint8_t subsel);

    virtio_input_config_t config_;

    static const size_t kEventCount = 64;
    io_buffer_t buffers_[kEventCount];

    fbl::Mutex lock_;

    uint8_t dev_class_;
    hidbus_protocol_ops_t hidbus_ops_;
    hidbus_ifc_protocol_t hidbus_ifc_;

    std::unique_ptr<HidDevice> hid_device_;
    Ring vring_ = {this};
};

} // namespace virtio
