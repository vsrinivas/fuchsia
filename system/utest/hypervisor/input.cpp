// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/vector.h>
#include <hid/hid.h>
#include <hid/usages.h>
#include <hypervisor/input.h>

#include <unittest/unittest.h>

// Yanked from system/ulib/hid/hid.c
#define KEYSET(bitmap, n) (bitmap[(n) >> 5] |= (1 << ((n)&31)))
#define KEYCLR(bitmap, n) (bitmap[(n) >> 5] &= ~(1 << ((n)&31)))

static const hid_keys_t kAllKeysUp = {};

/* Event emitter that records all queued events for verification purposes. */
class FakeEventEmitter : public VirtioInputEventEmitter {
public:
    virtual zx_status_t QueueInputEvent(const virtio_input_event_t& event) override {
        if (flushed_) {
            fprintf(stderr, "FakeEventEmitter has been flushed. Call Reset() to queue more "
                            "events.\n");
            return ZX_ERR_BAD_STATE;
        }
        queued_events_.push_back(event);
        return ZX_OK;
    }

    virtual zx_status_t FlushInputEvents() override {
        flushed_ = true;
        return ZX_OK;
    }

    void Reset() {
        flushed_ = false;
        queued_events_.reset();
    }

    size_t events() { return queued_events_.size(); }

    virtio_input_event_t event(size_t index) {
        return queued_events_[index];
    }

    // Check for an evdev event between |min| and |max| inclusive in the
    // output stream.
    bool HasEvent(size_t min, size_t max, uint16_t type, uint16_t code, uint32_t value) {
        for (size_t i = min; i < max + 1 && i < queued_events_.size(); ++i) {
            auto event = queued_events_[i];
            if (event.type == type && event.value == value && event.code == code) {
                return true;
            }
        }
        return false;
    }

    bool HasKeyPress(size_t min, size_t max, uint16_t usage) {
        uint16_t code = KeyboardEventSource::kKeyMap[usage];
        return HasEvent(min, max, VIRTIO_INPUT_EV_KEY, code, VIRTIO_INPUT_EV_KEY_PRESSED);
    }

    bool HasKeyRelease(size_t min, size_t max, uint16_t usage) {
        uint16_t code = KeyboardEventSource::kKeyMap[usage];
        return HasEvent(min, max, VIRTIO_INPUT_EV_KEY, code, VIRTIO_INPUT_EV_KEY_RELEASED);
    }

    bool HasBarrier(size_t index) {
        return HasEvent(index, index, VIRTIO_INPUT_EV_SYN, 0, 0);
    }

private:
    bool flushed_ = false;
    fbl::Vector<virtio_input_event_t> queued_events_;
};

static bool test_key_press(void) {
    BEGIN_TEST;

    FakeEventEmitter emitter;
    KeyboardEventSource keyboard(&emitter, 0);

    // Set 'A' as pressed.
    hid_keys_t keys = {};
    KEYSET(keys.keymask, HID_USAGE_KEY_A);

    ASSERT_EQ(keyboard.HandleHidKeys(keys), ZX_OK);

    ASSERT_EQ(emitter.events(), 2);
    EXPECT_TRUE(emitter.HasKeyPress(0, 0, HID_USAGE_KEY_A));
    EXPECT_TRUE(emitter.HasBarrier(1));

    END_TEST;
}

static bool test_key_press_multiple(void) {
    BEGIN_TEST;

    FakeEventEmitter emitter;
    KeyboardEventSource keyboard(&emitter, 0);

    // Set 'ABCD' as pressed.
    hid_keys_t keys = {};
    KEYSET(keys.keymask, HID_USAGE_KEY_A);
    KEYSET(keys.keymask, HID_USAGE_KEY_B);
    KEYSET(keys.keymask, HID_USAGE_KEY_C);
    KEYSET(keys.keymask, HID_USAGE_KEY_D);

    ASSERT_EQ(keyboard.HandleHidKeys(keys), ZX_OK);

    ASSERT_EQ(emitter.events(), 5);
    EXPECT_TRUE(emitter.HasKeyPress(0, 3, HID_USAGE_KEY_A));
    EXPECT_TRUE(emitter.HasKeyPress(0, 3, HID_USAGE_KEY_B));
    EXPECT_TRUE(emitter.HasKeyPress(0, 3, HID_USAGE_KEY_C));
    EXPECT_TRUE(emitter.HasKeyPress(0, 3, HID_USAGE_KEY_D));
    EXPECT_TRUE(emitter.HasBarrier(4));

    END_TEST;
}

static bool test_key_release(void) {
    BEGIN_TEST;

    FakeEventEmitter emitter;
    KeyboardEventSource keyboard(&emitter, 0);

    // Initialize with 'A' key pressed.
    hid_keys_t key_pressed_keys = {};
    KEYSET(key_pressed_keys.keymask, HID_USAGE_KEY_A);
    ASSERT_EQ(keyboard.HandleHidKeys(key_pressed_keys), ZX_OK);
    emitter.Reset();

    // Release all keys.
    ASSERT_EQ(keyboard.HandleHidKeys(kAllKeysUp), ZX_OK);

    ASSERT_EQ(emitter.events(), 2);
    EXPECT_TRUE(emitter.HasKeyRelease(0, 0, HID_USAGE_KEY_A));
    EXPECT_TRUE(emitter.HasBarrier(1));

    END_TEST;
}

static bool test_key_release_multiple(void) {
    BEGIN_TEST;

    FakeEventEmitter emitter;
    KeyboardEventSource keyboard(&emitter, 0);

    // Set 'ABCD' as pressed.
    hid_keys_t keys = {};
    KEYSET(keys.keymask, HID_USAGE_KEY_A);
    KEYSET(keys.keymask, HID_USAGE_KEY_B);
    KEYSET(keys.keymask, HID_USAGE_KEY_C);
    KEYSET(keys.keymask, HID_USAGE_KEY_D);
    ASSERT_EQ(keyboard.HandleHidKeys(keys), ZX_OK);
    emitter.Reset();

    // Release all keys.
    ASSERT_EQ(keyboard.HandleHidKeys(kAllKeysUp), ZX_OK);

    ASSERT_EQ(emitter.events(), 5);
    EXPECT_TRUE(emitter.HasKeyRelease(0, 3, HID_USAGE_KEY_A));
    EXPECT_TRUE(emitter.HasKeyRelease(0, 3, HID_USAGE_KEY_B));
    EXPECT_TRUE(emitter.HasKeyRelease(0, 3, HID_USAGE_KEY_C));
    EXPECT_TRUE(emitter.HasKeyRelease(0, 3, HID_USAGE_KEY_D));
    EXPECT_TRUE(emitter.HasBarrier(4));

    END_TEST;
}

// Test keys both being pressed and released in a single HID report.
static bool test_key_press_and_release(void) {
    BEGIN_TEST;

    FakeEventEmitter emitter;
    KeyboardEventSource keyboard(&emitter, 0);

    // Set 'AB' as pressed.
    hid_keys_t keys_ab = {};
    KEYSET(keys_ab.keymask, HID_USAGE_KEY_A);
    KEYSET(keys_ab.keymask, HID_USAGE_KEY_B);
    ASSERT_EQ(keyboard.HandleHidKeys(keys_ab), ZX_OK);
    emitter.Reset();

    // Release 'AB' and press 'CD'.
    hid_keys_t keys_cd = {};
    KEYSET(keys_cd.keymask, HID_USAGE_KEY_C);
    KEYSET(keys_cd.keymask, HID_USAGE_KEY_D);
    ASSERT_EQ(keyboard.HandleHidKeys(keys_cd), ZX_OK);

    ASSERT_EQ(emitter.events(), 5);
    EXPECT_TRUE(emitter.HasKeyPress(0, 3, HID_USAGE_KEY_C));
    EXPECT_TRUE(emitter.HasKeyPress(0, 3, HID_USAGE_KEY_D));
    EXPECT_TRUE(emitter.HasKeyRelease(0, 3, HID_USAGE_KEY_A));
    EXPECT_TRUE(emitter.HasKeyRelease(0, 3, HID_USAGE_KEY_B));
    EXPECT_TRUE(emitter.HasBarrier(4));

    END_TEST;
}

BEGIN_TEST_CASE(virtio_input)
RUN_TEST(test_key_press);
RUN_TEST(test_key_press_multiple);
RUN_TEST(test_key_release);
RUN_TEST(test_key_release_multiple);
RUN_TEST(test_key_press_and_release);
END_TEST_CASE(virtio_input)
