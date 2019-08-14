// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_processor.h"

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"
#include "src/media/audio/effects/test_effects/test_effects.h"
#include "src/media/audio/lib/effects_loader/effects_loader_test_base.h"

namespace media::audio {
namespace {

class EffectsProcessorTest : public test::EffectsLoaderTestBase {
 protected:
  EffectsProcessor effects_processor_{effects_loader(), 48000};
};

//
// The following tests validates the EffectsProcessor class itself.
//
// Verify the creation, uniqueness, quantity and deletion of effect instances.
TEST_F(EffectsProcessorTest, CreateDelete) {
  ASSERT_EQ(ZX_OK, test_effects()->add_effect({{"assign_to_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                                FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                               TEST_EFFECTS_ACTION_ASSIGN,
                                               1.0}));

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
}

// Verify the chain's positioning -- during insertion, reorder, deletion.
TEST_F(EffectsProcessorTest, Reorder) {
  ASSERT_EQ(ZX_OK, test_effects()->add_effect({{"assign_to_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                                FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                               TEST_EFFECTS_ACTION_ASSIGN,
                                               1.0}));

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
  ASSERT_EQ(ZX_OK,
            test_effects()->add_effect({{"increment_by_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                         FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                        TEST_EFFECTS_ACTION_ADD,
                                        1.0}));
  ASSERT_EQ(ZX_OK,
            test_effects()->add_effect({{"increment_by_2.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                         FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                        TEST_EFFECTS_ACTION_ADD,
                                        2.0}));
  ASSERT_EQ(ZX_OK,
            test_effects()->add_effect({{"assign_to_12.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                         FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                        TEST_EFFECTS_ACTION_ASSIGN,
                                        12.0}));
  ASSERT_EQ(ZX_OK,
            test_effects()->add_effect({{"increment_by_4.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                         FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                        TEST_EFFECTS_ACTION_ADD,
                                        4.0}));

  float buff[4] = {0, 1.0, 2.0, 3.0};

  // Before instances added, ProcessInPlace and Flush should succeed.
  EXPECT_EQ(effects_processor_.ProcessInPlace(4, buff), ZX_OK);
  EXPECT_EQ(effects_processor_.Flush(), ZX_OK);

  // Chaining four instances together, ProcessInPlace and flush should succeed.
  fuchsia_audio_effects_handle_t handle1 = effects_processor_.CreateFx(0, 1, 1, 0, {});
  fuchsia_audio_effects_handle_t handle2 = effects_processor_.CreateFx(1, 1, 1, 1, {});
  fuchsia_audio_effects_handle_t handle3 = effects_processor_.CreateFx(2, 1, 1, 2, {});
  fuchsia_audio_effects_handle_t handle4 = effects_processor_.CreateFx(3, 1, 1, 3, {});

  ASSERT_TRUE(handle1 != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE &&
              handle2 != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE &&
              handle3 != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE &&
              handle4 != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
  EXPECT_EQ(4u, test_effects()->num_instances());

  // The first 2 processors will mutate data, but this will be clobbered by the 3rd processor which
  // just sets every sample to 12.0. The final processor will increment by 4.0 resulting in the
  // expected 16.0 values.
  EXPECT_EQ(effects_processor_.ProcessInPlace(4, buff), ZX_OK);
  EXPECT_EQ(buff[0], 16.0);
  EXPECT_EQ(buff[1], 16.0);
  EXPECT_EQ(buff[2], 16.0);
  EXPECT_EQ(buff[3], 16.0);

  // All effects should have initial flush count 0.
  test_effects_inspect_state inspect1 = {};
  test_effects_inspect_state inspect2 = {};
  test_effects_inspect_state inspect3 = {};
  test_effects_inspect_state inspect4 = {};
  EXPECT_EQ(ZX_OK, test_effects()->inspect_instance(handle1, &inspect1));
  EXPECT_EQ(ZX_OK, test_effects()->inspect_instance(handle2, &inspect2));
  EXPECT_EQ(ZX_OK, test_effects()->inspect_instance(handle3, &inspect3));
  EXPECT_EQ(ZX_OK, test_effects()->inspect_instance(handle4, &inspect4));
  EXPECT_EQ(0u, inspect1.flush_count);
  EXPECT_EQ(0u, inspect2.flush_count);
  EXPECT_EQ(0u, inspect3.flush_count);
  EXPECT_EQ(0u, inspect4.flush_count);

  // Flush, just sanity test the test_effects library has observed the flush call on each effect.
  EXPECT_EQ(effects_processor_.Flush(), ZX_OK);
  EXPECT_EQ(ZX_OK, test_effects()->inspect_instance(handle1, &inspect1));
  EXPECT_EQ(ZX_OK, test_effects()->inspect_instance(handle2, &inspect2));
  EXPECT_EQ(ZX_OK, test_effects()->inspect_instance(handle3, &inspect3));
  EXPECT_EQ(ZX_OK, test_effects()->inspect_instance(handle4, &inspect4));
  EXPECT_EQ(1u, inspect1.flush_count);
  EXPECT_EQ(1u, inspect2.flush_count);
  EXPECT_EQ(1u, inspect3.flush_count);
  EXPECT_EQ(1u, inspect4.flush_count);

  // Zero num_frames is valid and should succeed. We assign the buff to some random values here
  // to ensure the processor does not clobber them.
  buff[0] = 20.0;
  buff[1] = 21.0;
  buff[2] = 22.0;
  buff[3] = 23.0;
  EXPECT_EQ(effects_processor_.ProcessInPlace(0, buff), ZX_OK);
  EXPECT_EQ(buff[0], 20.0);
  EXPECT_EQ(buff[1], 21.0);
  EXPECT_EQ(buff[2], 22.0);
  EXPECT_EQ(buff[3], 23.0);

  // If no buffer provided, ProcessInPlace should fail (even if 0 num_frames).
  EXPECT_NE(effects_processor_.ProcessInPlace(0, nullptr), ZX_OK);

  // With all instances removed, ProcessInPlace and Flush should still succeed.
  EXPECT_EQ(effects_processor_.DeleteFx(handle1), ZX_OK);
  EXPECT_EQ(effects_processor_.DeleteFx(handle2), ZX_OK);
  EXPECT_EQ(effects_processor_.DeleteFx(handle3), ZX_OK);
  EXPECT_EQ(effects_processor_.DeleteFx(handle4), ZX_OK);

  EXPECT_EQ(0u, test_effects()->num_instances());
  EXPECT_EQ(effects_processor_.ProcessInPlace(4, buff), ZX_OK);
  EXPECT_EQ(buff[0], 20.0);
  EXPECT_EQ(buff[1], 21.0);
  EXPECT_EQ(buff[2], 22.0);
  EXPECT_EQ(buff[3], 23.0);

  EXPECT_EQ(effects_processor_.Flush(), ZX_OK);
}

}  // namespace
}  // namespace media::audio
