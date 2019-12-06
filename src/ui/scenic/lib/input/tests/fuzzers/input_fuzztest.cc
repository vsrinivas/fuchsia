// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <fuzzer/FuzzedDataProvider.h>

#include "src/ui/scenic/lib/input/tests/util.h"

namespace lib_ui_input_tests {

class FuzzInputSystemTest : public InputSystemTest {
 public:
  FuzzInputSystemTest(uint32_t display_width, uint32_t display_height) : InputSystemTest(), display_width_(display_width), display_height_(display_height) { InputSystemTest::SetUp(); }

  ~FuzzInputSystemTest() { InputSystemTest::TearDown(); }

  void RunLoopUntilIdle() { InputSystemTest::RunLoopUntilIdle(); }

 protected:
  uint32_t test_display_width_px() const override { return display_width_; }
  uint32_t test_display_height_px() const override { return display_height_; }

 private:
  uint32_t display_width_;
  uint32_t display_height_;

  void TestBody() override {}
};

// Create a fuzzed pointer command.
fuchsia::ui::input::SendPointerInputCmd CreatePointerCmd(FuzzedDataProvider& fuzzed_data) {
  fuchsia::ui::input::PointerEvent pointer_event;
  pointer_event.type = static_cast<fuchsia::ui::input::PointerEventType>(fuzzed_data.ConsumeIntegral<uint32_t>());
  pointer_event.event_time = fuzzed_data.ConsumeIntegral<uint64_t>();
  pointer_event.device_id = fuzzed_data.ConsumeIntegral<uint32_t>();
  pointer_event.pointer_id = fuzzed_data.ConsumeIntegral<uint32_t>();
  pointer_event.phase = static_cast<fuchsia::ui::input::PointerEventPhase>(fuzzed_data.ConsumeIntegral<uint32_t>());
  pointer_event.x = fuzzed_data.ConsumeFloatingPoint<float>();
  pointer_event.y = fuzzed_data.ConsumeFloatingPoint<float>();

  fuchsia::ui::input::SendPointerInputCmd pointer_cmd;
  pointer_cmd.compositor_id = fuzzed_data.ConsumeIntegral<uint32_t>();
  pointer_cmd.pointer_event = std::move(pointer_event);

  return pointer_cmd;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
  // Create and enqueue fuzzed pointer commands.
  FuzzedDataProvider fuzzed_data(Data, Size);

  // Create an input system.
  FuzzInputSystemTest input_system_test(/*display_width=*/fuzzed_data.ConsumeIntegral<uint32_t>(),
                                        /*display_height=*/fuzzed_data.ConsumeIntegral<uint32_t>());

  // Create a small scene with a view and child view containing shape to hit.

  // Setup scene.
  auto [root_session, root_resources] = input_system_test.CreateScene();
  scenic::Session* const session = root_session.session();
  scenic::Scene* const scene = &root_resources.scene;
  auto [view_token1, view_holder_token1] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();
  scenic::ViewHolder view_holder1(session, std::move(view_holder_token1), "view_holder1");
  view_holder1.SetViewProperties(InputSystemTest::k5x5x1);
  scene->AddChild(view_holder1);
  input_system_test.RequestToPresent(session);

  // Create initial view.
  SessionWrapper client1(input_system_test.scenic());
  auto pair = scenic::ViewRefPair::New();
  client1.SetViewKoid(scenic_impl::gfx::ExtractKoid(pair.view_ref));
  scenic::View view(client1.session(), std::move(view_token1), std::move(pair.control_ref),
                    std::move(pair.view_ref), "client1");
  scenic::ViewHolder view_holder2(client1.session(), std::move(view_holder_token2), "view_holder2");
  view_holder2.SetViewProperties(InputSystemTest::k5x5x1);
  view.AddChild(view_holder2);
  input_system_test.RequestToPresent(client1.session());

  // Create child view.
  SessionWrapper client2 = input_system_test.CreateClient("client2", std::move(view_token2));

  // Fuzz and send input.
  while (fuzzed_data.remaining_bytes() > 0) {
    fuchsia::ui::input::Command input_cmd;
    input_cmd.set_send_pointer_input(CreatePointerCmd(fuzzed_data));
    root_session.session()->Enqueue(std::move(input_cmd));
  }

  // Run the loop and see if we crash.
  input_system_test.RunLoopUntilIdle();

  return 0;
}

}  // namespace lib_ui_input_tests
