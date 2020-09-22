// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/trace-provider/provider.h>

#include <array>
#include <iomanip>

#include <virtio/input.h>

#include "src/virtualization/bin/vmm/device/device_base.h"
#include "src/virtualization/bin/vmm/device/input.h"
#include "src/virtualization/bin/vmm/device/stream_base.h"

// HID usage -> evdev keycode.
static constexpr std::array<uint8_t, 232> kKeyMap{
    0,    // Reserved
    0,    // Keyboard ErrorRollOver
    0,    // Keyboard POSTFail
    0,    // Keyboard ErrorUndefined
    30,   // A
    48,   // B
    46,   // C
    32,   // D
    18,   // E
    33,   // F
    34,   // G
    35,   // H
    23,   // I
    36,   // J
    37,   // K
    38,   // L
    50,   // M
    49,   // N
    24,   // O
    25,   // P
    16,   // Q
    19,   // R
    31,   // S
    20,   // T
    22,   // U
    47,   // V
    17,   // W
    45,   // X
    21,   // Y
    44,   // Z
    2,    // 1
    3,    // 2
    4,    // 3
    5,    // 4
    6,    // 5
    7,    // 6
    8,    // 7
    9,    // 8
    10,   // 9
    11,   // 0
    28,   // Enter
    1,    // Esc
    14,   // Backspace
    15,   // Tab
    57,   // Space
    12,   // -
    13,   // =
    26,   // [
    27,   // ]
    43,   // Backslash
    43,   // Non-US # and ~
    39,   // ;
    40,   // '
    41,   // `
    51,   // ,
    52,   // .
    53,   // /
    58,   // Caps Lock
    59,   // F1
    60,   // F2
    61,   // F3
    62,   // F4
    63,   // F5
    64,   // F6
    65,   // F7
    66,   // F8
    67,   // F9
    68,   // F10
    87,   // F11
    88,   // F12
    99,   // Print Screen
    70,   // ScrollLock
    119,  // Pause
    110,  // Insert
    102,  // Home
    104,  // PageUp
    111,  // Delete Forward
    107,  // End
    109,  // PageDown
    106,  // Right
    105,  // Left
    108,  // Down
    103,  // Up
    69,   // NumLock
    98,   // Keypad /
    55,   // Keypad *
    74,   // Keypad -
    78,   // Keypad +
    96,   // Keypad Enter
    79,   // Keypad 1
    80,   // Keypad 2
    81,   // Keypad 3
    75,   // Keypad 4
    76,   // Keypad 5
    77,   // Keypad 6
    71,   // Keypad 7
    72,   // Keypad 8
    73,   // Keypad 9
    82,   // Keypad 0
    83,   // Keypad .
    86,   // Non-US \ and |
    127,  // Keyboard Application
    116,  // Power
    117,  // Keypad =
    183,  // F13
    184,  // F14
    185,  // F15
    186,  // F16
    187,  // F17
    188,  // F18
    189,  // F19
    190,  // F20
    191,  // F21
    192,  // F22
    193,  // F23
    194,  // F24
    134,  // Execute
    138,  // Help
    130,  // Menu
    132,  // Select
    128,  // Stop
    129,  // Again
    131,  // Undo
    137,  // Cut
    133,  // Copy
    135,  // Paste
    136,  // Find
    113,  // Mute
    115,  // Volume Up
    114,  // Volume Down

    // Skip some more esoteric keys that have no obvious evdev counterparts.
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    29,   // Left Ctrl
    42,   // Left Shift
    56,   // Left Alt
    125,  // Left Meta
    97,   // Right Ctrl
    54,   // Right Shift
    100,  // Right Alt
    126,  // Right Meta
};

enum class Queue : uint16_t {
  EVENT = 0,
  STATUS = 1,
};

static uint16_t key_or_repeat(fuchsia::ui::input::KeyboardEventPhase phase) {
  return phase == fuchsia::ui::input::KeyboardEventPhase::REPEAT ? VIRTIO_INPUT_EV_REP
                                                                 : VIRTIO_INPUT_EV_KEY;
}

static uint32_t press_or_release(fuchsia::ui::input::KeyboardEventPhase phase) {
  return phase == fuchsia::ui::input::KeyboardEventPhase::PRESSED ||
                 phase == fuchsia::ui::input::KeyboardEventPhase::REPEAT
             ? VIRTIO_INPUT_EV_KEY_PRESSED
             : VIRTIO_INPUT_EV_KEY_RELEASED;
}

static uint32_t press_or_release(fuchsia::ui::input::PointerEventPhase phase) {
  return phase == fuchsia::ui::input::PointerEventPhase::DOWN ? VIRTIO_INPUT_EV_KEY_PRESSED
                                                              : VIRTIO_INPUT_EV_KEY_RELEASED;
}

// Retrieves the position of a pointer event and translates it into the
// coordinate space expected in the VIRTIO_INPUT_EV_ABS event payload. The
// incoming event coordinates are expected to be in the floating-point 0..1
// range, which are mapped to the nearest integer in 0..kAbsMax[X/Y].
//
// TODO(fxbug.dev/24138): pointer event positions outside view boundaries.
static uint32_t x_coordinate(float x, float width) {
  if (x < 0.0f || x > width) {
    FX_LOGS(WARNING) << "PointerEvent::x out of range (" << std::fixed << std::setprecision(7) << x
                     << ")";
    x = std::clamp(x, 0.0f, width);
  }
  return static_cast<uint32_t>(x * kInputAbsMaxX / width + 0.5f);
}

static uint32_t y_coordinate(float y, float height) {
  if (y < 0.0f || y > height) {
    FX_LOGS(WARNING) << "PointerEvent::y out of range (" << std::fixed << std::setprecision(7) << y
                     << ")";
    y = std::clamp(y, 0.0f, height);
  }
  return static_cast<uint32_t>(y * kInputAbsMaxY / height + 0.5f);
}

// Stream for event queue.
class EventStream : public StreamBase, public fuchsia::virtualization::hardware::ViewListener {
 public:
  EventStream(sys::ComponentContext* context) {
    context->outgoing()->AddPublicService(view_listener_bindings_.GetHandler(this));
  }

  void DoEvent() {
    for (; !RingIsEmpty() && queue_.NextChain(&chain_); chain_.Return()) {
      while (!RingIsEmpty() && chain_.NextDescriptor(&desc_)) {
        auto event = static_cast<virtio_input_event_t*>(desc_.addr);
        DequeueEvent(event);
        *Used() += sizeof(*event);
      }
    }
  }

 private:
  fidl::BindingSet<ViewListener> view_listener_bindings_;
  std::array<virtio_input_event_t, 64> event_ring_;
  size_t head_ = 0;
  size_t tail_ = 0;
  fuchsia::ui::gfx::vec3 view_size_ = {0, 0, 0};

  void OnKeyboard(const fuchsia::ui::input::KeyboardEvent& keyboard) {
    uint32_t hid_usage = keyboard.hid_usage;
    if (hid_usage >= kKeyMap.size()) {
      FX_LOGS(WARNING) << "Unsupported keyboard event";
      return;
    }
    virtio_input_event_t events[] = {
        {
            .type = key_or_repeat(keyboard.phase),
            .code = kKeyMap[hid_usage],
            .value = press_or_release(keyboard.phase),
        },
        {
            .type = VIRTIO_INPUT_EV_SYN,
        },
    };
    bool enqueued = EnqueueEvents(events);
    if (!enqueued) {
      FX_LOGS(WARNING) << "Dropped keyboard event";
    }
  }

  void OnPointer(const fuchsia::ui::input::PointerEvent& pointer) {
    switch (pointer.phase) {
      case fuchsia::ui::input::PointerEventPhase::MOVE: {
        virtio_input_event_t events[] = {
            {
                .type = VIRTIO_INPUT_EV_ABS,
                .code = VIRTIO_INPUT_EV_ABS_X,
                .value = x_coordinate(pointer.x, view_size_.x),
            },
            {
                .type = VIRTIO_INPUT_EV_ABS,
                .code = VIRTIO_INPUT_EV_ABS_Y,
                .value = y_coordinate(pointer.y, view_size_.y),
            },
            {
                .type = VIRTIO_INPUT_EV_SYN,
            },
        };
        bool enqueued = EnqueueEvents(events);
        if (!enqueued) {
          FX_LOGS(WARNING) << "Dropped pointer event";
        }
        return;
      }
      case fuchsia::ui::input::PointerEventPhase::DOWN:
      case fuchsia::ui::input::PointerEventPhase::UP: {
        virtio_input_event_t events[] = {
            {
                .type = VIRTIO_INPUT_EV_ABS,
                .code = VIRTIO_INPUT_EV_ABS_X,
                .value = x_coordinate(pointer.x, view_size_.x),
            },
            {
                .type = VIRTIO_INPUT_EV_ABS,
                .code = VIRTIO_INPUT_EV_ABS_Y,
                .value = y_coordinate(pointer.y, view_size_.y),
            },
            {
                .type = VIRTIO_INPUT_EV_KEY,
                .code = kButtonTouchCode,
                .value = press_or_release(pointer.phase),
            },
            {
                .type = VIRTIO_INPUT_EV_SYN,
            },
        };
        bool enqueued = EnqueueEvents(events);
        if (!enqueued) {
          FX_LOGS(WARNING) << "Dropped pointer event";
        }
        return;
      }
      default:
        return;
    }
  }

  // |fuchsia::virtualization::hardware::ViewListener|
  void OnInputEvent(fuchsia::ui::input::InputEvent event) override {
    switch (event.Which()) {
      case fuchsia::ui::input::InputEvent::Tag::kKeyboard:
        OnKeyboard(event.keyboard());
        break;
      case fuchsia::ui::input::InputEvent::Tag::kPointer:
        if (view_size_.x > 0 && view_size_.y > 0) {
          OnPointer(event.pointer());
        }
        break;
      default:
        return;
    }

    DoEvent();
  }

  // |fuchsia::virtualization::hardware::ViewListener|
  void OnSizeChanged(fuchsia::ui::gfx::vec3 size) override { view_size_ = size; }

  template <size_t N>
  bool EnqueueEvents(virtio_input_event_t (&events)[N]) {
    if (RingFree() < N) {
      return false;
    }
    for (auto& event : events) {
      event_ring_[tail_] = event;
      tail_ = RingIndex(tail_ + 1);
    }
    return true;
  }

  void DequeueEvent(virtio_input_event_t* event) {
    *event = event_ring_[head_];
    head_ = RingIndex(head_ + 1);
  }

  size_t RingIndex(size_t index) const { return index % event_ring_.size(); }
  size_t RingFree() const { return (head_ - tail_ - 1) % event_ring_.size(); }
  bool RingIsEmpty() const { return head_ == tail_; }
};

// Implementation of a virtio-input device.
class VirtioInputImpl : public DeviceBase<VirtioInputImpl>,
                        public fuchsia::virtualization::hardware::VirtioInput {
 public:
  VirtioInputImpl(sys::ComponentContext* context) : DeviceBase(context), event_stream_(context) {}

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void NotifyQueue(uint16_t queue) override {
    switch (static_cast<Queue>(queue)) {
      case Queue::EVENT:
        event_stream_.DoEvent();
        break;
      case Queue::STATUS:
        break;
      default:
        FX_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
  }

 private:
  // |fuchsia::virtualization::hardware::VirtioInput|
  void Start(fuchsia::virtualization::hardware::StartInfo start_info,
             StartCallback callback) override {
    auto deferred = fit::defer(std::move(callback));
    PrepStart(std::move(start_info));
    event_stream_.Init(
        phys_mem_, fit::bind_member<zx_status_t, DeviceBase>(this, &VirtioInputImpl::Interrupt));
  }

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                      zx_gpaddr_t used, ConfigureQueueCallback callback) override {
    auto deferred = fit::defer(std::move(callback));
    switch (static_cast<Queue>(queue)) {
      case Queue::EVENT:
        event_stream_.Configure(size, desc, avail, used);
        break;
      case Queue::STATUS:
        break;
      default:
        FX_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
  }

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void Ready(uint32_t negotiated_features, ReadyCallback callback) override { callback(); }

  EventStream event_stream_;
};

int main(int argc, char** argv) {
  syslog::SetTags({"virtio_input"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();

  VirtioInputImpl virtio_input(context.get());
  return loop.Run();
}
