// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_processor.h"

#include <gtest/gtest.h>

namespace media::audio {
namespace {

class EffectsProcessorTest : public testing::Test {
 public:
  void SetUp() override { ASSERT_EQ(effects_loader_.LoadLibrary(), ZX_OK); }
  void TearDown() override { ASSERT_EQ(effects_loader_.UnloadLibrary(), ZX_OK); }

 protected:
  EffectsLoader effects_loader_{"audio_effects.so"};
  EffectsProcessor effects_processor_{&effects_loader_, 48000};
};

//
// The following tests validates the EffectsProcessor class itself.
//
// Verify the creation, uniqueness, quantity and deletion of effect instances.
TEST_F(EffectsProcessorTest, CreateDelete) {
  fuchsia_audio_effects_handle_t handle3 = effects_processor_.CreateFx(0, 1, 1, 0, {});
  fuchsia_audio_effects_handle_t handle1 = effects_processor_.CreateFx(0, 1, 1, 0, {});
  fuchsia_audio_effects_handle_t handle2 = effects_processor_.CreateFx(0, 1, 1, 1, {});
  fuchsia_audio_effects_handle_t handle4 = effects_processor_.CreateFx(0, 1, 1, 3, {});

  ASSERT_TRUE(handle1 != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE &&
              handle2 != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE &&
              handle3 != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE &&
              handle4 != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);

  EXPECT_TRUE(handle1 != handle2 && handle1 != handle3 && handle1 != handle4 &&
              handle2 != handle3 && handle2 != handle4 && handle3 != handle4);

  EXPECT_EQ(effects_processor_.GetNumFx(), 4);

  fuchsia_audio_effects_handle_t handle5 = effects_processor_.CreateFx(0, 1, 1, 5, {});
  EXPECT_EQ(handle5, FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);

  // Remove one of the four instances.
  EXPECT_EQ(effects_processor_.DeleteFx(handle3), ZX_OK);
  EXPECT_EQ(effects_processor_.GetNumFx(), 3);

  // Remove a second instance.
  EXPECT_EQ(effects_processor_.DeleteFx(handle4), ZX_OK);
  EXPECT_EQ(effects_processor_.GetNumFx(), 2);

  // This handle has already been removed.
  EXPECT_NE(effects_processor_.DeleteFx(handle3), ZX_OK);
  EXPECT_EQ(effects_processor_.GetNumFx(), 2);

  // Remove a third instance -- only one should remain.
  EXPECT_EQ(effects_processor_.DeleteFx(handle1), ZX_OK);
  EXPECT_EQ(effects_processor_.GetNumFx(), 1);

  // Invalid handle cannot be removed/deleted.
  EXPECT_NE(effects_processor_.DeleteFx(FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE), ZX_OK);
  EXPECT_EQ(effects_processor_.GetNumFx(), 1);

  // Remove fourth and last instance.
  EXPECT_EQ(effects_processor_.DeleteFx(handle2), ZX_OK);
  EXPECT_EQ(effects_processor_.GetNumFx(), 0);

  // This handle has already been removed -- also empty chain.
  EXPECT_NE(effects_processor_.DeleteFx(handle4), ZX_OK);
  EXPECT_EQ(effects_processor_.GetNumFx(), 0);

  // Inserting an instance into a chain that has been populated, then emptied.
  EXPECT_NE(effects_processor_.CreateFx(0, 1, 1, 0, {}), FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
  EXPECT_EQ(effects_processor_.GetNumFx(), 1);

  // Leave an active instance, to exercise the destructor cleanup.
}

// Verify the chain's positioning -- during insertion, reorder, deletion.
TEST_F(EffectsProcessorTest, Reorder) {
  fuchsia_audio_effects_handle_t handle2 = effects_processor_.CreateFx(0, 1, 1, 0, {});
  fuchsia_audio_effects_handle_t handle1 = effects_processor_.CreateFx(0, 1, 1, 0, {});
  fuchsia_audio_effects_handle_t handle4 = effects_processor_.CreateFx(0, 1, 1, 2, {});
  fuchsia_audio_effects_handle_t handle3 = effects_processor_.CreateFx(0, 1, 1, 2, {});
  // Chain is [2], then [1,2], then [1,2,4], then [1,2,3,4].

  ASSERT_TRUE(handle1 != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE &&
              handle2 != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE &&
              handle3 != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE &&
              handle4 != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);

  // Chain is [1,2,3,4].
  EXPECT_EQ(effects_processor_.GetFxAt(0), handle1);
  EXPECT_EQ(effects_processor_.GetFxAt(1), handle2);
  EXPECT_EQ(effects_processor_.GetFxAt(2), handle3);
  EXPECT_EQ(effects_processor_.GetFxAt(3), handle4);
  EXPECT_EQ(effects_processor_.GetFxAt(4), FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);

  // Moving handle4 to position 2: [1,2,3,4] becomes [1,2,4,3].
  EXPECT_EQ(effects_processor_.ReorderFx(handle4, 2), ZX_OK);
  EXPECT_EQ(effects_processor_.GetFxAt(0), handle1);
  EXPECT_EQ(effects_processor_.GetFxAt(1), handle2);
  EXPECT_EQ(effects_processor_.GetFxAt(2), handle4);
  EXPECT_EQ(effects_processor_.GetFxAt(3), handle3);
  EXPECT_EQ(effects_processor_.GetFxAt(4), FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);

  // Moving handle1 to position 2: [1,2,4,3] becomes [2,4,1,3].
  EXPECT_EQ(effects_processor_.ReorderFx(handle1, 2), ZX_OK);
  EXPECT_EQ(effects_processor_.GetFxAt(0), handle2);
  EXPECT_EQ(effects_processor_.GetFxAt(1), handle4);
  EXPECT_EQ(effects_processor_.GetFxAt(2), handle1);
  EXPECT_EQ(effects_processor_.GetFxAt(3), handle3);
  EXPECT_EQ(effects_processor_.GetFxAt(4), FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);

  // Position 4 is outside the chain. No change: chain is still [2,4,1,3].
  EXPECT_NE(effects_processor_.ReorderFx(handle2, 4), ZX_OK);
  EXPECT_EQ(effects_processor_.GetFxAt(0), handle2);
  EXPECT_EQ(effects_processor_.GetFxAt(1), handle4);
  EXPECT_EQ(effects_processor_.GetFxAt(2), handle1);
  EXPECT_EQ(effects_processor_.GetFxAt(3), handle3);
  EXPECT_EQ(effects_processor_.GetFxAt(4), FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);

  // Removing handle1: [2,4,1,3] becomes [2,4,3].
  EXPECT_EQ(effects_processor_.DeleteFx(handle1), ZX_OK);
  EXPECT_EQ(effects_processor_.GetFxAt(0), handle2);
  EXPECT_EQ(effects_processor_.GetFxAt(1), handle4);
  EXPECT_EQ(effects_processor_.GetFxAt(2), handle3);
  EXPECT_EQ(effects_processor_.GetFxAt(3), FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);

  // Removing handle2 (from front): [2,4,3] becomes [4,3].
  EXPECT_EQ(effects_processor_.DeleteFx(handle2), ZX_OK);
  EXPECT_EQ(effects_processor_.GetFxAt(0), handle4);
  EXPECT_EQ(effects_processor_.GetFxAt(1), handle3);
  EXPECT_EQ(effects_processor_.GetFxAt(2), FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);

  // Removing handle3 (from end): [4,3] becomes [4].
  EXPECT_EQ(effects_processor_.DeleteFx(handle3), ZX_OK);
  EXPECT_EQ(effects_processor_.GetFxAt(0), handle4);
  EXPECT_EQ(effects_processor_.GetFxAt(1), FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);

  // Removing handle4: [4] becomes [].
  EXPECT_EQ(effects_processor_.DeleteFx(handle4), ZX_OK);
  EXPECT_EQ(effects_processor_.GetFxAt(0), FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
}

// Verify (at a VERY Basic level) the methods that handle data flow.
TEST_F(EffectsProcessorTest, ProcessInPlaceFlush) {
  // TODO(MTWN-405): Use a non-passthrough processor that mutates data in an observable way to
  // ensure the effect chain is processing in the correct order.
  float buff[4] = {0, 0, 0, 0};

  // Before instances added, ProcessInPlace and Flush should succeed.
  EXPECT_EQ(effects_processor_.ProcessInPlace(4, buff), ZX_OK);
  EXPECT_EQ(effects_processor_.Flush(), ZX_OK);

  // Chaining four instances together, ProcessInPlace and flush should succeed.
  fuchsia_audio_effects_handle_t handle1 = effects_processor_.CreateFx(0, 1, 1, 0, {});
  fuchsia_audio_effects_handle_t handle2 = effects_processor_.CreateFx(0, 1, 1, 1, {});
  fuchsia_audio_effects_handle_t handle3 = effects_processor_.CreateFx(0, 1, 1, 2, {});
  fuchsia_audio_effects_handle_t handle4 = effects_processor_.CreateFx(0, 1, 1, 3, {});

  ASSERT_TRUE(handle1 != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE &&
              handle2 != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE &&
              handle3 != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE &&
              handle4 != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);

  EXPECT_EQ(effects_processor_.ProcessInPlace(4, buff), ZX_OK);
  EXPECT_EQ(effects_processor_.Flush(), ZX_OK);
  EXPECT_EQ(effects_processor_.ProcessInPlace(4, buff), ZX_OK);

  // Zero num_frames is valid and should succeed.
  EXPECT_EQ(effects_processor_.ProcessInPlace(0, buff), ZX_OK);

  // If no buffer provided, ProcessInPlace should fail (even if 0 num_frames).
  EXPECT_NE(effects_processor_.ProcessInPlace(0, nullptr), ZX_OK);

  // With all instances removed, ProcessInPlace and Flush should still succeed.
  EXPECT_EQ(effects_processor_.DeleteFx(handle1), ZX_OK);
  EXPECT_EQ(effects_processor_.DeleteFx(handle2), ZX_OK);
  EXPECT_EQ(effects_processor_.DeleteFx(handle3), ZX_OK);
  EXPECT_EQ(effects_processor_.DeleteFx(handle4), ZX_OK);
  EXPECT_EQ(effects_processor_.ProcessInPlace(4, buff), ZX_OK);
  EXPECT_EQ(effects_processor_.Flush(), ZX_OK);
}

}  // namespace
}  // namespace media::audio
