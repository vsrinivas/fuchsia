// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/input.h>

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fdio/watcher.h>
#include <hypervisor/bits.h>
#include <hypervisor/vcpu.h>
#include <hypervisor/virtio.h>
#include <virtio/input.h>
#include <virtio/virtio.h>
#include <virtio/virtio_ids.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

static const char* kInputDirPath = "/dev/class/input";

// HID keycode -> evdev keycode.
const uint8_t KeyboardEventSource::kKeyMap[] = {
    0,   // Reserved
    0,   // Keyboard ErrorRollOver
    0,   // Keyboard POSTFail
    0,   // Keyboard ErrorUndefined
    30,  // A
    48,  // B
    46,  // C
    32,  // D
    18,  // E
    33,  // F
    34,  // G
    35,  // H
    23,  // I
    36,  // J
    37,  // K
    38,  // L
    50,  // M
    49,  // N
    24,  // O
    25,  // P
    16,  // Q
    19,  // R
    31,  // S
    20,  // T
    22,  // U
    47,  // V
    17,  // W
    45,  // X
    21,  // Y
    44,  // Z
    2,   // 1
    3,   // 2
    4,   // 3
    5,   // 4
    6,   // 5
    7,   // 6
    8,   // 7
    9,   // 8
    10,  // 9
    11,  // 0
    28,  // Enter
    1,   // Esc
    14,  // Backspace
    15,  // Tab
    57,  // Space
    12,  // -
    13,  // =
    26,  // [
    27,  // ]
    43,  // Backslash
    43,  // Non-US # and ~
    39,  // ;
    40,  // '
    41,  // `
    51,  // ,
    52,  // .
    53,  // /
    58,  // Caps Lock
    59,  // F1
    60,  // F2
    61,  // F3
    62,  // F4
    63,  // F5
    64,  // F6
    65,  // F7
    66,  // F8
    67,  // F9
    68,  // F10
    87,  // F11
    88,  // F12
    99,  // Print Screen
    70,  // ScrollLock
    119, // Pause
    110, // Insert
    102, // Home
    104, // PageUp
    111, // Delete Forward
    107, // End
    109, // PageDown
    106, // Right
    105, // Left
    108, // Down
    103, // Up
    69,  // NumLock
    98,  // Keypad /
    55,  // Keypad *
    74,  // Keypad -
    78,  // Keypad +
    96,  // Keypad Enter
    79,  // Keypad 1
    80,  // Keypad 2
    81,  // Keypad 3
    75,  // Keypad 4
    76,  // Keypad 5
    77,  // Keypad 6
    71,  // Keypad 7
    72,  // Keypad 8
    73,  // Keypad 9
    82,  // Keypad 0
    83,  // Keypad .
    86,  // Non-US \ and |
    127, // Keyboard Application
    116, // Power
    117, // Keypad =
    183, // F13
    184, // F14
    185, // F15
    186, // F16
    187, // F17
    188, // F18
    189, // F19
    190, // F20
    191, // F21
    192, // F22
    193, // F23
    194, // F24
    134, // Execute
    138, // Help
    130, // Menu
    132, // Select
    128, // Stop
    129, // Again
    131, // Undo
    137, // Cut
    133, // Copy
    135, // Paste
    136, // Find
    113, // Mute
    115, // Volume Up
    114, // Volume Down

    // Skip some more esoteric keys that have no obvious evdev counterparts.
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    29,  // Left Ctrl
    42,  // Left Shift
    56,  // Left Alt
    125, // Left Meta
    97,  // Right Ctrl
    54,  // Right Shift
    100, // Right Alt
    126, // Right Meta
};

VirtioInput::VirtioInput(uintptr_t guest_physmem_addr, size_t guest_physmem_size,
                         const char* device_name, const char* device_serial)
    : VirtioDevice(VIRTIO_ID_INPUT, &config_, sizeof(config_), queues_, VIRTIO_INPUT_Q_COUNT,
                   guest_physmem_addr, guest_physmem_size),
      device_name_(device_name), device_serial_(device_serial) {}

zx_status_t VirtioInput::WriteConfig(uint64_t port, const IoValue& value) {
    zx_status_t status = VirtioDevice::WriteConfig(port, value);
    if (status != ZX_OK)
        return status;
    if (port >= 2)
        return ZX_OK;

    //  A write to select or subselect modifies the contents of the config.u
    //  field.
    fbl::AutoLock lock(&config_mutex_);
    switch (config_.select) {
    case VIRTIO_INPUT_CFG_ID_NAME: {
        size_t len = strlen(device_name_);
        memcpy(&config_.u, device_name_, len);
        config_.size = static_cast<uint8_t>(len > sizeof(config_.u) ? sizeof(config_.u) : len);
        return ZX_OK;
    }
    case VIRTIO_INPUT_CFG_ID_SERIAL: {
        size_t len = strlen(device_serial_);
        config_.size = static_cast<uint8_t>(len > sizeof(config_.u) ? sizeof(config_.u) : len);
        memcpy(&config_.u, device_serial_, len);
        return ZX_OK;
    }

    // VIRTIO_INPUT_CFG_EV_BITS: subsel specifies the event type (EV_*).
    // If size is non-zero the event type is supported and a bitmap the of
    // supported event codes is returned in u.bitmap.
    case VIRTIO_INPUT_CFG_EV_BITS: {
        if (config_.subsel == VIRTIO_INPUT_EV_KEY) {
            // Say we support all key events. This isn't true but it's
            // simple.
            memset(&config_.u, 0xff, sizeof(config_.u));
            config_.size = sizeof(config_.u);
            return ZX_OK;
        }
        // Fall-through.
    }
    case VIRTIO_INPUT_CFG_UNSET:
    case VIRTIO_INPUT_CFG_ID_DEVIDS:
    case VIRTIO_INPUT_CFG_PROP_BITS:
    case VIRTIO_INPUT_CFG_ABS_INFO:
        memset(&config_.u, 0, sizeof(config_.u));
        config_.size = 0;
        return ZX_OK;
    default:
        fprintf(stderr, "unsupported select value %u\n", config_.select);
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
}

static int watch_input_directory_thread(void* input) {
    int dirfd = open(kInputDirPath, O_DIRECTORY | O_RDONLY);
    if (dirfd < 0)
        return ZX_ERR_IO;

    zx_status_t status = fdio_watch_directory(dirfd, &VirtioInput::AddInputDevice,
                                              ZX_TIME_INFINITE, input);

    close(dirfd);
    return status;
}

zx_status_t VirtioInput::Start() {
    thrd_t thread;
    int ret = thrd_create(&thread, &watch_input_directory_thread, this);
    if (ret != thrd_success) {
        return ZX_ERR_INTERNAL;
    }
    ret = thrd_detach(thread);
    if (ret != thrd_success) {
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
}

zx_status_t VirtioInput::QueueInputEvent(const virtio_input_event_t& event) {
    uint16_t head;
    virtio_queue_wait(&event_queue(), &head);

    virtio_desc_t desc;
    zx_status_t status = virtio_queue_read_desc(&event_queue(), head, &desc);
    if (status != ZX_OK)
        return status;

    auto event_out = static_cast<virtio_input_event_t*>(desc.addr);
    memcpy(event_out, &event, sizeof(event));
    virtio_queue_return(&event_queue(), head, sizeof(event));
    return ZX_OK;
}

// static
zx_status_t VirtioInput::AddInputDevice(int dirfd, int event, const char* fn, void* cookie) {
    auto input = static_cast<VirtioInput*>(cookie);
    if (event != WATCH_EVENT_ADD_FILE) {
        return ZX_OK;
    }

    int fd = openat(dirfd, fn, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open device %s/%s\n", kInputDirPath, fn);
        return ZX_OK;
    }

    auto closer = fbl::MakeAutoCall([fd]() { close(fd); });

    int proto = INPUT_PROTO_NONE;
    if (ioctl_input_get_protocol(fd, &proto) < 0) {
        fprintf(stderr, "Failed to get input device protocol.\n");
        return ZX_ERR_INVALID_ARGS;
    }

    // If the device isn't a keyboard, just continue.
    if (proto != INPUT_PROTO_KBD)
        return ZX_OK;

    fbl::AllocChecker ac;
    auto keyboard = fbl::make_unique_checked<KeyboardEventSource>(&ac, input, fd);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    zx_status_t status = keyboard->Start();
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to start device %s/%s\n", kInputDirPath, fn);
        return status;
    }
    fprintf(stderr, "virtio-input: Polling device %s/%s for key events.\n", kInputDirPath, fn);

    closer.cancel();
    fbl::AutoLock lock(&input->mutex_);
    input->keyboards_.push_front(fbl::move(keyboard));
    return ZX_OK;
}

KeyboardEventSource::~KeyboardEventSource() {
    if (fd_ >= 0)
        close(fd_);
}

static int hid_event_thread(void* cookie) {
    return reinterpret_cast<KeyboardEventSource*>(cookie)->HidEventLoop();
}

zx_status_t KeyboardEventSource::Start() {
    thrd_t thread;
    int ret = thrd_create(&thread, &hid_event_thread, this);
    if (ret != thrd_success) {
        return ZX_ERR_INTERNAL;
    }
    ret = thrd_detach(thread);
    if (ret != thrd_success) {
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
}

zx_status_t KeyboardEventSource::HidEventLoop() {
    uint8_t report[8];
    while (true) {
        ssize_t r = read(fd_, report, sizeof(report));
        if (r != sizeof(report)) {
            fprintf(stderr, "failed to read from input device\n");
            return ZX_ERR_IO;
        }

        hid_keys_t curr_keys;
        hid_kbd_parse_report(report, &curr_keys);

        zx_status_t status = HandleHidKeys(curr_keys);
        if (status != ZX_OK) {
            fprintf(stderr, "Failed to handle HID keys.\n");
            return status;
        }
    }
    return ZX_OK;
}

zx_status_t KeyboardEventSource::HandleHidKeys(const hid_keys_t& curr_keys) {
    // Send key-down events.
    uint8_t keycode;
    hid_keys_t pressed;
    hid_kbd_pressed_keys(&prev_keys_, &curr_keys, &pressed);
    hid_for_every_key(&pressed, keycode) {
        zx_status_t status = SendKeyEvent(keycode, true);
        if (status != ZX_OK)
            return status;
    }

    // Send key-up events.
    hid_keys_t released;
    hid_kbd_released_keys(&prev_keys_, &curr_keys, &released);
    hid_for_every_key(&released, keycode) {
      zx_status_t status = SendKeyEvent(keycode, false);
      if (status != ZX_OK)
          return status;
    }

    prev_keys_ = curr_keys;
    return SendBarrierEvent();
}

zx_status_t KeyboardEventSource::SendKeyEvent(uint32_t scancode, bool pressed) {
    if (scancode >= sizeof(kKeyMap) / sizeof(kKeyMap[0])) {
        // Unknown key.
        return ZX_OK;
    }

    virtio_input_event_t event;
    event.type = VIRTIO_INPUT_EV_KEY;
    event.code = kKeyMap[scancode];
    event.value = pressed ? VIRTIO_INPUT_EV_KEY_PRESSED : VIRTIO_INPUT_EV_KEY_RELEASED;
    return emitter_->QueueInputEvent(event);
}

zx_status_t KeyboardEventSource::SendBarrierEvent() {
    virtio_input_event_t event;
    event.type = VIRTIO_INPUT_EV_SYN;
    event.code = 0;
    event.value = 0;
    zx_status_t status = emitter_->QueueInputEvent(event);
    if (status != ZX_OK)
        return status;
    return emitter_->FlushInputEvents();
}
