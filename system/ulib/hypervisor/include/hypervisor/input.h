// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <fbl/intrusive_single_list.h>
#include <fbl/unique_ptr.h>
#include <hid/hid.h>
#include <hypervisor/virtio.h>
#include <virtio/input.h>
#include <zircon/compiler.h>
#include <zircon/device/input.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#define VIRTIO_INPUT_Q_EVENTQ 0
#define VIRTIO_INPUT_Q_STATUSQ 1
#define VIRTIO_INPUT_Q_COUNT 2

/* Interface for manipulating the stream of input events. */
class VirtioInputEventEmitter {
public:
    virtual ~VirtioInputEventEmitter() = default;

    virtual zx_status_t QueueInputEvent(const virtio_input_event_t& event);

    virtual zx_status_t FlushInputEvents() = 0;
};

/* Manages input events from a single (host) keyboard device. */
class KeyboardEventSource : public fbl::SinglyLinkedListable<fbl::unique_ptr<KeyboardEventSource>> {
public:
    // Map HID scancodes to evdev keycodes.
    //
    // See include/uapi/linux/input-event-codes.h in the linux kernel for the full
    // set of evdev keycodes.
    static const uint8_t kKeyMap[];

    KeyboardEventSource(VirtioInputEventEmitter* emitter, int fd)
        : fd_(fd), emitter_(emitter) {}

    ~KeyboardEventSource();

    // Compares |keys| against the previous report to infer which keys have
    // been pressed or released. Sends a corresponding evdev event for each
    // key press/release.
    zx_status_t HandleHidKeys(const hid_keys_t& keys);

    // Spawn a thread to read key reports from the keyboard device.
    zx_status_t Start();

    zx_status_t HidEventLoop();
private:
    // Sends an evdev key event.
    zx_status_t SendKeyEvent(uint32_t scancode, bool pressed);

    // Send an evdev barrier event to mark the end of a sequence of events.
    zx_status_t SendBarrierEvent();

    int fd_ = -1;
    hid_keys_t prev_keys_ = {};
    VirtioInputEventEmitter* emitter_;
};

/* Virtio input device. */
class VirtioInput : public VirtioDevice, public VirtioInputEventEmitter {
public:
    VirtioInput(uintptr_t guest_physmem_addr, size_t guest_physmem_size,
                const char* device_name, const char* device_serial);

    zx_status_t WriteConfig(uint64_t port, const IoValue& value) override;

    virtio_queue_t& event_queue() { return queues_[VIRTIO_INPUT_Q_EVENTQ]; }

    // Spawns a thread to monitor for new input devices. When one is detected
    // the corresponding event source will be created to poll for events.
    zx_status_t Start();

    // VirtioInputEventEmitter interface.
    //
    // |QueueInputEvents| will write packets to the event queue, but no
    // interrupt will be generated to the guest until |FlushInputEvents| is
    // called.
    zx_status_t QueueInputEvent(const virtio_input_event_t& event) override;
    zx_status_t FlushInputEvents() override { return NotifyGuest(); }

    // Invoked when new devices are added.
    static zx_status_t AddInputDevice(int dirfd, int event, const char* fn, void* cookie);
private:

    fbl::Mutex mutex_;
    const char* device_name_;
    const char* device_serial_;
    virtio_queue_t queues_[VIRTIO_INPUT_Q_COUNT];
    virtio_input_config_t config_ TA_GUARDED(config_mutex_) = {};

    fbl::SinglyLinkedList<fbl::unique_ptr<KeyboardEventSource>> keyboards_ TA_GUARDED(mutex_);
};
