// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/input/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/input3/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/zx/status.h>

#include <virtio/input.h>

#include "src/virtualization/bin/vmm/device/input.h"
#include "src/virtualization/bin/vmm/device/tests/test_with_device.h"
#include "src/virtualization/bin/vmm/device/tests/virtio_queue_fake.h"

namespace {

constexpr uint16_t kNumQueues = 2;
constexpr uint16_t kQueueSize = 16;

constexpr auto kComponentUrl = "#meta/virtio_input.cm";

struct VirtioInputTestParam {
  std::string test_name;
  bool configure_status_queue;
};

class VirtioInputTest : public TestWithDevice,
                        public ::testing::WithParamInterface<VirtioInputTestParam> {
 protected:
  VirtioInputTest()
      : event_queue_(phys_mem_, PAGE_SIZE * kNumQueues, kQueueSize),
        status_queue_(phys_mem_, event_queue_.end(), kQueueSize) {}

  void SetUp() override {
    using component_testing::ChildRef;
    using component_testing::ParentRef;
    using component_testing::Protocol;
    using component_testing::RealmBuilder;
    using component_testing::RealmRoot;
    using component_testing::Route;

    constexpr auto kComponentName = "virtio_input";

    auto realm_builder = RealmBuilder::Create();
    realm_builder.AddChild(kComponentName, kComponentUrl);

    realm_builder
        .AddRoute(Route{.capabilities =
                            {
                                Protocol{fuchsia::logger::LogSink::Name_},
                                Protocol{fuchsia::tracing::provider::Registry::Name_},
                            },
                        .source = ParentRef(),
                        .targets = {ChildRef{kComponentName}}})
        .AddRoute(
            Route{.capabilities =
                      {
                          Protocol{fuchsia::virtualization::hardware::KeyboardListener::Name_},
                          Protocol{fuchsia::virtualization::hardware::PointerListener::Name_},
                          Protocol{fuchsia::virtualization::hardware::VirtioInput::Name_},
                      },
                  .source = ChildRef{kComponentName},
                  .targets = {ParentRef()}});

    realm_ = std::make_unique<RealmRoot>(realm_builder.Build(dispatcher()));

    fuchsia::virtualization::hardware::StartInfo start_info;
    zx_status_t status = MakeStartInfo(event_queue_.end(), &start_info);
    ASSERT_EQ(ZX_OK, status);

    keyboard_listener_ = realm_->Connect<fuchsia::virtualization::hardware::KeyboardListener>();
    pointer_listener_ = realm_->ConnectSync<fuchsia::virtualization::hardware::PointerListener>();
    input_ = realm_->ConnectSync<fuchsia::virtualization::hardware::VirtioInput>();

    status = input_->Start(std::move(start_info));
    ASSERT_EQ(ZX_OK, status);

    // Configure device queues.
    VirtioQueueFake* queues[kNumQueues] = {&event_queue_, &status_queue_};
    for (uint16_t i = 0; i < kNumQueues; i++) {
      auto q = queues[i];
      q->Configure(PAGE_SIZE * i, PAGE_SIZE);
      status = input_->ConfigureQueue(i, q->size(), q->desc(), q->avail(), q->used());
      ASSERT_EQ(ZX_OK, status);

      if (!GetParam().configure_status_queue) {
        break;
      }
    }

    // Finish negotiating features.
    status = input_->Ready(0);
    ASSERT_EQ(ZX_OK, status);
  }

  zx::status<std::vector<virtio_input_event_t*>> AddEventDescriptorsToChain(size_t n) {
    std::vector<virtio_input_event_t*> result(n);
    // Note: virtio-input sends only one virtio_input_event_t per chain.
    for (size_t i = 0; i < n; ++i) {
      virtio_input_event_t* event;
      zx_status_t status = DescriptorChainBuilder(event_queue_)
                               .AppendWritableDescriptor(&event, sizeof(*event))
                               .Build();
      if (status != ZX_OK) {
        return zx::error(status);
      }
      result[i] = event;
    }
    return zx::ok(std::move(result));
  }

  // Note: use of sync can be problematic here if the test environment needs to handle
  // some incoming FIDL requests.
  fuchsia::virtualization::hardware::VirtioInputSyncPtr input_;
  fuchsia::virtualization::hardware::KeyboardListenerPtr keyboard_listener_;
  fuchsia::virtualization::hardware::PointerListenerSyncPtr pointer_listener_;
  VirtioQueueFake event_queue_;
  VirtioQueueFake status_queue_;
  std::unique_ptr<component_testing::RealmRoot> realm_;
};

TEST_P(VirtioInputTest, Keyboard) {
  // Enqueue descriptors for the events. Do this before we inject the key event because the device
  // may choose to drop input events if there are no descriptors available:
  //
  // 5.8.6.2 Device Requirements: Device Operation
  //
  // A device MAY drop input events if the eventq does not have enough available buffers.
  auto events = AddEventDescriptorsToChain(2);
  ASSERT_TRUE(events.is_ok());

  zx_status_t status = input_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);

  // Inject a key event.
  std::optional<fuchsia::ui::input3::KeyEventStatus> key_status = std::nullopt;
  fuchsia::ui::input3::KeyEvent keyboard;
  keyboard.set_type(fuchsia::ui::input3::KeyEventType::PRESSED);
  keyboard.set_key(fuchsia::input::Key::A);
  keyboard_listener_->OnKeyEvent(std::move(keyboard), [&](auto result) { key_status = result; });

  // Expect the virtio event.
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  // Verify we received 2 events; key press + sync.
  auto event_1 = events.value()[0];
  EXPECT_EQ(VIRTIO_INPUT_EV_KEY, event_1->type);
  EXPECT_EQ(30, event_1->code);
  EXPECT_EQ(VIRTIO_INPUT_EV_KEY_PRESSED, event_1->value);
  auto event_2 = events.value()[1];
  EXPECT_EQ(VIRTIO_INPUT_EV_SYN, event_2->type);
}

TEST_P(VirtioInputTest, PointerMove) {
  // TODO(fxbug.dev/104229): Enable this test.
  GTEST_SKIP();

  pointer_listener_->OnSizeChanged({1, 1});
  fuchsia::ui::input::PointerEvent pointer = {
      .phase = fuchsia::ui::input::PointerEventPhase::MOVE,
      .x = 0.25,
      .y = 0.5,
  };
  pointer_listener_->OnPointerEvent(std::move(pointer));

  virtio_input_event_t* event_1;
  virtio_input_event_t* event_2;
  virtio_input_event_t* event_3;
  zx_status_t status = DescriptorChainBuilder(event_queue_)
                           .AppendWritableDescriptor(&event_1, sizeof(*event_1))
                           .AppendWritableDescriptor(&event_2, sizeof(*event_2))
                           .AppendWritableDescriptor(&event_3, sizeof(*event_3))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = input_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_INPUT_EV_ABS, event_1->type);
  EXPECT_EQ(VIRTIO_INPUT_EV_ABS_X, event_1->code);
  EXPECT_EQ(static_cast<uint32_t>(std::ceil(kInputAbsMaxX * pointer.x)), event_1->value);
  EXPECT_EQ(VIRTIO_INPUT_EV_ABS, event_2->type);
  EXPECT_EQ(VIRTIO_INPUT_EV_ABS_Y, event_2->code);
  EXPECT_EQ(static_cast<uint32_t>(std::ceil(kInputAbsMaxY * pointer.y)), event_2->value);
  EXPECT_EQ(VIRTIO_INPUT_EV_SYN, event_3->type);
}

TEST_P(VirtioInputTest, PointerUp) {
  // TODO(fxbug.dev/104229): Enable this test.
  GTEST_SKIP();

  pointer_listener_->OnSizeChanged({1, 1});
  fuchsia::ui::input::PointerEvent pointer = {
      .phase = fuchsia::ui::input::PointerEventPhase::UP,
      .x = 0.25,
      .y = 0.5,
  };
  pointer_listener_->OnPointerEvent(std::move(pointer));

  virtio_input_event_t* event_1;
  virtio_input_event_t* event_2;
  virtio_input_event_t* event_3;
  virtio_input_event_t* event_4;
  zx_status_t status = DescriptorChainBuilder(event_queue_)
                           .AppendWritableDescriptor(&event_1, sizeof(*event_1))
                           .AppendWritableDescriptor(&event_2, sizeof(*event_2))
                           .AppendWritableDescriptor(&event_3, sizeof(*event_3))
                           .AppendWritableDescriptor(&event_4, sizeof(*event_4))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = input_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_INPUT_EV_ABS, event_1->type);
  EXPECT_EQ(VIRTIO_INPUT_EV_ABS_X, event_1->code);
  EXPECT_EQ(static_cast<uint32_t>(std::ceil(kInputAbsMaxX * pointer.x)), event_1->value);
  EXPECT_EQ(VIRTIO_INPUT_EV_ABS, event_2->type);
  EXPECT_EQ(VIRTIO_INPUT_EV_ABS_Y, event_2->code);
  EXPECT_EQ(static_cast<uint32_t>(std::ceil(kInputAbsMaxY * pointer.y)), event_2->value);
  EXPECT_EQ(VIRTIO_INPUT_EV_KEY, event_3->type);
  EXPECT_EQ(kButtonTouchCode, event_3->code);
  EXPECT_EQ(VIRTIO_INPUT_EV_KEY_RELEASED, event_3->value);
  EXPECT_EQ(VIRTIO_INPUT_EV_SYN, event_4->type);
}

INSTANTIATE_TEST_SUITE_P(VirtioInputComponentsTest, VirtioInputTest,
                         testing::Values(VirtioInputTestParam{"statusq", true},
                                         VirtioInputTestParam{"nostatusq", false}),
                         [](const testing::TestParamInfo<VirtioInputTestParam>& info) {
                           return info.param.test_name;
                         });

}  // namespace
