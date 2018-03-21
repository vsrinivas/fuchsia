// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "input.h"

#include <limits.h>
#include <string.h>

#include <ddk/debug.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/device/input.h>
#include <zircon/status.h>

#include "trace.h"

#define LOCAL_TRACE 0

namespace virtio {

// clang-format off

const uint8_t kEventCodeMap[] = {
    0,                                  // KEY_RESERVED (0)
    41,                                 // KEY_ESC (1)
    30,                                 // KEY_1 (2)
    31,                                 // KEY_2 (3)
    32,                                 // KEY_3 (4)
    33,                                 // KEY_4 (5)
    34,                                 // KEY_5 (6)
    35,                                 // KEY_6 (7)
    36,                                 // KEY_7 (8)
    37,                                 // KEY_8 (9)
    38,                                 // KEY_9 (10)
    39,                                 // KEY_0 (11)
    45,                                 // KEY_MINUS (12)
    46,                                 // KEY_EQUAL (13)
    42,                                 // KEY_BACKSPACE (14)
    43,                                 // KEY_TAB (15)
    20,                                 // KEY_Q (16)
    26,                                 // KEY_W (17)
    8,                                  // KEY_E (18)
    21,                                 // KEY_R (19)
    23,                                 // KEY_T (20)
    28,                                 // KEY_Y (21)
    24,                                 // KEY_U (22)
    12,                                 // KEY_I (23)
    18,                                 // KEY_O (24)
    19,                                 // KEY_P (25)
    47,                                 // KEY_LEFTBRACE (26)
    48,                                 // KEY_RIGHTBRACE (27)
    40,                                 // KEY_ENTER (28)
    224,                                // KEY_LEFTCTRL (29)
    4,                                  // KEY_A (30)
    22,                                 // KEY_S (31)
    7,                                  // KEY_D (32)
    9,                                  // KEY_F (33)
    10,                                 // KEY_G (34)
    11,                                 // KEY_H (35)
    13,                                 // KEY_J (36)
    14,                                 // KEY_K (37)
    15,                                 // KEY_L (38)
    51,                                 // KEY_SEMICOLON (39)
    52,                                 // KEY_APOSTROPHE (40)
    53,                                 // KEY_GRAVE (41)
    225,                                // KEY_LEFTSHIFT (42)
    49,                                 // KEY_BACKSLASH (43)
    29,                                 // KEY_Z (44)
    27,                                 // KEY_X (45)
    6,                                  // KEY_C (46)
    25,                                 // KEY_V (47)
    5,                                  // KEY_B (48)
    17,                                 // KEY_N (49)
    16,                                 // KEY_M (50)
    54,                                 // KEY_COMMA (51)
    55,                                 // KEY_DOT (52)
    56,                                 // KEY_SLASH (53)
    229,                                // KEY_RIGHTSHIFT (54)
    85,                                 // KEY_KPASTERISK (55)
    226,                                // KEY_LEFTALT (56)
    44,                                 // KEY_SPACE (57)
    57,                                 // KEY_CAPSLOCK (58)
    58,                                 // KEY_F1 (59)
    59,                                 // KEY_F2 (60)
    60,                                 // KEY_F3 (61)
    61,                                 // KEY_F4 (62)
    62,                                 // KEY_F5 (63)
    63,                                 // KEY_F6 (64)
    64,                                 // KEY_F7 (65)
    65,                                 // KEY_F8 (66)
    66,                                 // KEY_F9 (67)
    67,                                 // KEY_F10 (68)
    83,                                 // KEY_NUMLOCK (69)
    71,                                 // KEY_SCROLLLOCK (70)
    95,                                 // KEY_KP7 (71)
    96,                                 // KEY_KP8 (72)
    97,                                 // KEY_KP9 (73)
    86,                                 // KEY_KPMINUS (74)
    92,                                 // KEY_KP4 (75)
    93,                                 // KEY_KP5 (76)
    94,                                 // KEY_KP6 (77)
    87,                                 // KEY_KPPLUS (78)
    89,                                 // KEY_KP1 (79)
    90,                                 // KEY_KP2 (80)
    91,                                 // KEY_KP3 (81)
    98,                                 // KEY_KP0 (82)
    99,                                 // KEY_KPDOT (83)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // Unsupported
    228,                                // KEY_RIGHTCTRL (97)
    0, 0,                               // Unsupported
    230,                                // KEY_RIGHTALT (100)
};

static const uint8_t kbd_hid_report_desc[] = {
    0x05, 0x01, // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06, // Usage (Keyboard)
    0xA1, 0x01, // Collection (Application)
    0x05, 0x07, //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0, //   Usage Minimum (0xE0)
    0x29, 0xE7, //   Usage Maximum (0xE7)
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0x01, //   Logical Maximum (1)
    0x75, 0x01, //   Report Size (1)
    0x95, 0x08, //   Report Count (8)
    0x81, 0x02, //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01, //   Report Count (1)
    0x75, 0x08, //   Report Size (8)
    0x81, 0x01, //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x05, //   Report Count (5)
    0x75, 0x01, //   Report Size (1)
    0x05, 0x08, //   Usage Page (LEDs)
    0x19, 0x01, //   Usage Minimum (Num Lock)
    0x29, 0x05, //   Usage Maximum (Kana)
    0x91, 0x02, //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,
                //   Non-volatile)
    0x95, 0x01, //   Report Count (1)
    0x75, 0x03, //   Report Size (3)
    0x91, 0x01, //   Output (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,
                //   Non-volatile)
    0x95, 0x06, //   Report Count (6)
    0x75, 0x08, //   Report Size (8)
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0x65, //   Logical Maximum (101)
    0x05, 0x07, //   Usage Page (Kbrd/Keypad)
    0x19, 0x00, //   Usage Minimum (0x00)
    0x29, 0x65, //   Usage Maximum (0x65)
    0x81, 0x00, //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,       // End Collection
};

// clang-format on

// DDK level ops

void InputDevice::virtio_input_release(void* ctx) {
    virtio::InputDevice* inp = static_cast<virtio::InputDevice*>(ctx);
    return inp->Release();
}

zx_status_t InputDevice::virtio_input_query(void* ctx, uint32_t options, hid_info_t* info) {
    virtio::InputDevice* inp = static_cast<virtio::InputDevice*>(ctx);
    return inp->Query(options, info);
}

zx_status_t InputDevice::virtio_input_get_descriptor(void* ctx, uint8_t desc_type,
                                                     void** data, size_t* len) {
    virtio::InputDevice* inp = static_cast<virtio::InputDevice*>(ctx);
    return inp->GetDescriptor(desc_type, data, len);
}

zx_status_t InputDevice::virtio_input_get_report(void* ctx, uint8_t rpt_type, uint8_t rpt_id,
                                                 void* data, size_t len, size_t* out_len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t InputDevice::virtio_input_set_report(void* ctx, uint8_t rpt_type, uint8_t rpt_id,
                                                 void* data, size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t InputDevice::virtio_input_get_idle(void* ctx, uint8_t rpt_type, uint8_t* duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t InputDevice::virtio_input_set_idle(void* ctx, uint8_t rpt_type, uint8_t duration) {
    return ZX_OK;
}

zx_status_t InputDevice::virtio_input_get_protocol(void* ctx, uint8_t* protocol) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t InputDevice::virtio_input_set_protocol(void* ctx, uint8_t protocol) {
    return ZX_OK;
}

zx_status_t InputDevice::virtio_input_start(void* ctx, hidbus_ifc_t* ifc, void* cookie) {
    virtio::InputDevice* inp = static_cast<virtio::InputDevice*>(ctx);
    return inp->Start(ifc, cookie);
}

void InputDevice::virtio_input_stop(void* ctx) {
    virtio::InputDevice* inp = static_cast<virtio::InputDevice*>(ctx);
    inp->Stop();
}

InputDevice::InputDevice(zx_device_t* bus_device, zx::bti bti, fbl::unique_ptr<Backend> backend)
    : Device(bus_device, fbl::move(bti), fbl::move(backend)) {}

InputDevice::~InputDevice() {}

zx_status_t InputDevice::Init() {
    LTRACEF("Device %p\n", this);

    fbl::AutoLock lock(&lock_);

    // Reset the device and read configuration
    DeviceReset();

    SelectConfig(VIRTIO_INPUT_CFG_ID_NAME, 0);
    LTRACEF_LEVEL(2, "name %s\n", config_.u.string);

    SelectConfig(VIRTIO_INPUT_CFG_ID_SERIAL, 0);
    LTRACEF_LEVEL(2, "serial %s\n", config_.u.string);

    SelectConfig(VIRTIO_INPUT_CFG_ID_DEVIDS, 0);
    if (config_.size >= sizeof(virtio_input_devids_t)) {
        LTRACEF_LEVEL(2, "bustype %d\n", config_.u.ids.bustype);
        LTRACEF_LEVEL(2, "vendor %d\n", config_.u.ids.vendor);
        LTRACEF_LEVEL(2, "product %d\n", config_.u.ids.product);
        LTRACEF_LEVEL(2, "version %d\n", config_.u.ids.version);
    }

    SelectConfig(VIRTIO_INPUT_CFG_EV_BITS, VIRTIO_INPUT_EV_KEY);
    uint8_t cfg_key_size = config_.size;
    SelectConfig(VIRTIO_INPUT_CFG_EV_BITS, VIRTIO_INPUT_EV_REL);
    uint8_t cfg_rel_size = config_.size;
    SelectConfig(VIRTIO_INPUT_CFG_EV_BITS, VIRTIO_INPUT_EV_ABS);
    uint8_t cfg_abs_size = config_.size;

    // Assume that if the device supports either relative or absolute events
    // that it's a pointer, otherwise as long as it supports key events it's a
    // keyboard.
    if (cfg_rel_size > 0 || cfg_abs_size > 0) {
        // Pointer
        dev_class_ = HID_DEV_CLASS_POINTER;
    } else if (cfg_key_size > 0) {
        // Keyboard
        dev_class_ = HID_DEV_CLASS_KBD;
    } else {
        return ZX_ERR_NOT_SUPPORTED;
    }

    DriverStatusAck();

    // Plan to clean up unless everything succeeds.
    auto cleanup = fbl::MakeAutoCall([this]() { Release(); });

    // Allocate the main vring
    zx_status_t status = vring_.Init(0, kEventCount);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to allocate vring: %s\n", zx_status_get_string(status));
        return status;
    }

    // Allocate event buffers for the ring.
    // TODO: Avoid multiple allocations, allocate enough for all buffers once.
    for (uint16_t id = 0; id < kEventCount; ++id) {
        static_assert(sizeof(virtio_input_event_t) <= PAGE_SIZE, "");
        status = io_buffer_init(&buffers_[id], bti_.get(), sizeof(virtio_input_event_t),
                                IO_BUFFER_RO | IO_BUFFER_CONTIG);
        if (status != ZX_OK) {
            zxlogf(ERROR, "Failed to allocate I/O buffers: %s\n", zx_status_get_string(status));
            return status;
        }
    }

    // Expose event buffers to the host
    vring_desc* desc = nullptr;
    uint16_t id;
    for (uint16_t i = 0; i < kEventCount; ++i) {
        desc = vring_.AllocDescChain(1, &id);
        if (desc == nullptr) {
            zxlogf(ERROR, "Failed to allocate descriptor chain\n");
            return ZX_ERR_NO_RESOURCES;
        }
        ZX_ASSERT(id < kEventCount);
        desc->addr = io_buffer_phys(&buffers_[id]);
        desc->len = sizeof(virtio_input_event_t);
        desc->flags |= VRING_DESC_F_WRITE;
        LTRACE_DO(virtio_dump_desc(desc));
        vring_.SubmitChain(id);
    }

    // Prepare the HID report buffer
    memset(&report_, 0, sizeof(report_));

    StartIrqThread();
    DriverStatusOk();

    device_ops_.release = virtio_input_release;

    hidbus_ops_.query = virtio_input_query;
    hidbus_ops_.start = virtio_input_start;
    hidbus_ops_.stop = virtio_input_stop;
    hidbus_ops_.get_descriptor = virtio_input_get_descriptor;
    hidbus_ops_.get_report = virtio_input_get_report;
    hidbus_ops_.set_report = virtio_input_set_report;
    hidbus_ops_.get_idle = virtio_input_get_idle;
    hidbus_ops_.set_idle = virtio_input_set_idle;
    hidbus_ops_.get_protocol = virtio_input_get_protocol;
    hidbus_ops_.set_protocol = virtio_input_set_protocol;

    hidbus_ifc_ = nullptr;

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "virtio-input";
    args.ctx = this;
    args.ops = &device_ops_;
    args.proto_id = ZX_PROTOCOL_HIDBUS;
    args.proto_ops = &hidbus_ops_;

    status = device_add(bus_device_, &args, &device_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to add device: %s\n", zx_status_get_string(status));
        device_ = nullptr;
        return status;
    }

    vring_.Kick();
    cleanup.cancel();
    return ZX_OK;
}

zx_status_t InputDevice::Start(hidbus_ifc_t* ifc, void* cookie) {
    fbl::AutoLock lock(&lock_);
    if (hidbus_ifc_ != nullptr) {
        return ZX_ERR_ALREADY_BOUND;
    }
    hidbus_ifc_ = ifc;
    hidbus_cookie_ = cookie;
    return ZX_OK;
}

void InputDevice::Stop() {
    fbl::AutoLock lock(&lock_);
    hidbus_ifc_ = nullptr;
    hidbus_cookie_ = nullptr;
}

void InputDevice::Release() {
    fbl::AutoLock lock(&lock_);
    hidbus_ifc_ = nullptr;
    hidbus_cookie_ = nullptr;
    for (size_t i = 0; i < kEventCount; ++i) {
        if (io_buffer_is_valid(&buffers_[i])) {
            io_buffer_release(&buffers_[i]);
        }
    }
}

zx_status_t InputDevice::Query(uint32_t options, hid_info_t* info) {
    info->dev_num = dev_class_; // Use type for dev_num for now.
    info->dev_class = dev_class_;
    info->boot_device = true;
    return ZX_OK;
}

zx_status_t InputDevice::GetDescriptor(uint8_t desc_type, void** data, size_t* len) {
    if (data == nullptr || len == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (desc_type != HID_DESC_TYPE_REPORT) {
        return ZX_ERR_NOT_FOUND;
    }
    const uint8_t* buf = nullptr;
    size_t buflen = 0;

    // TODO: Handle devices other than keyboards;
    buf = static_cast<const uint8_t*>(kbd_hid_report_desc);
    buflen = sizeof(kbd_hid_report_desc);

    *data = malloc(buflen);
    if (*data == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }
    *len = buflen;
    memcpy(*data, buf, buflen);
    return ZX_OK;
}

void InputDevice::AddKeypressToReport(uint16_t event_code) {
    uint8_t hid_code = kEventCodeMap[event_code];
    for (size_t i = 0; i != 6; ++i) {
        if (report_.usage[i] == hid_code) {
            // The key already exists in the report so we ignore it.
            return;
        }
        if (report_.usage[i] == 0) {
            report_.usage[i] = hid_code;
            return;
        }
    }

    // There's no free slot in the report.
    // TODO: Record a rollover status.
}

void InputDevice::RemoveKeypressFromReport(uint16_t event_code) {
    uint8_t hid_code = kEventCodeMap[event_code];
    int id = -1;
    for (int i = 0; i != 6; ++i) {
        if (report_.usage[i] == hid_code) {
            id = i;
            break;
        }
    }

    if (id == -1) {
        // They key is not in the report so we ignore it.
        return;
    }

    for (size_t i = id; i != 5; ++i) {
        report_.usage[i] = report_.usage[i + 1];
    }
    report_.usage[5] = 0;
}

void InputDevice::ReceiveEvent(virtio_input_event_t* event) {
    // TODO: Support other event types (once we support more than a fake HID keyboard).
    if (event->type == VIRTIO_INPUT_EV_KEY) {
        if (event->code == 0) {
            return;
        }
        if (event->code >= countof(kEventCodeMap)) {
            LTRACEF("unknown key\n");
            return;
        }
        if (event->value == VIRTIO_INPUT_EV_KEY_PRESSED) {
            AddKeypressToReport(event->code);
        } else if (event->value == VIRTIO_INPUT_EV_KEY_RELEASED) {
            RemoveKeypressFromReport(event->code);
        }
    } else if (event->type == VIRTIO_INPUT_EV_SYN) {
        fbl::AutoLock lock(&lock_);
        if (hidbus_ifc_) {
            hidbus_ifc_->io_queue(hidbus_cookie_,
                                  reinterpret_cast<const uint8_t*>(&report_),
                                  sizeof(report_));
        }
    }
}

void InputDevice::IrqRingUpdate() {
    auto free_chain = [this](vring_used_elem* used_elem) {
        uint16_t id = static_cast<uint16_t>(used_elem->id & 0xffff);
        vring_desc* desc = vring_.DescFromIndex(id);
        ZX_ASSERT(id < kEventCount);
        ZX_ASSERT(desc->len == sizeof(virtio_input_event_t));

        auto evt = static_cast<virtio_input_event_t*>(io_buffer_virt(&buffers_[id]));
        ReceiveEvent(evt);

        ZX_ASSERT((desc->flags & VRING_DESC_F_NEXT) == 0);
        vring_.FreeDesc(id);
    };

    vring_.IrqRingUpdate(free_chain);

    vring_desc* desc = nullptr;
    uint16_t id;
    bool need_kick = false;
    while ((desc = vring_.AllocDescChain(1, &id))) {
        desc->len = sizeof(virtio_input_event_t);
        vring_.SubmitChain(id);
        need_kick = true;
    }

    if (need_kick) {
        vring_.Kick();
    }
}

void InputDevice::IrqConfigChange() {
    LTRACEF("IrqConfigChange\n");
}

void InputDevice::SelectConfig(uint8_t select, uint8_t subsel) {
    WriteDeviceConfig(offsetof(virtio_input_config_t, select), select);
    WriteDeviceConfig(offsetof(virtio_input_config_t, subsel), subsel);
    CopyDeviceConfig(&config_, sizeof(config_));
}

} // namespace virtio
