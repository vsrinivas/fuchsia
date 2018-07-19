// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>
#include <cmath>

#include "garnet/public/lib/media/audio_dfx/audio_device_fx.h"
#include "garnet/public/lib/media/audio_dfx/lib/dfx_base.h"
#include "garnet/public/lib/media/audio_dfx/lib/dfx_delay.h"
#include "garnet/public/lib/media/audio_dfx/lib/dfx_rechannel.h"
#include "garnet/public/lib/media/audio_dfx/lib/dfx_swap.h"
#include "gtest/gtest.h"

namespace media {
namespace audio_dfx_test {

//
// Tests of the DfxBase shared library interface.
//
class DfxBaseTest : public testing::Test {
 protected:
  // This SetUp function, invoked before each test runs, dyna-loads the shared
  // library and the symbolic entry points for each ABI function.
  void SetUp() override {
    dfx_lib_ = dlopen("libaudio_dfx.so", RTLD_LAZY | RTLD_GLOBAL);
    ASSERT_NE(dfx_lib_, nullptr) << "libaudio_dfx did not load";

    fn_get_num_fx_ = (bool (*)(uint32_t*))dlsym(
        dfx_lib_, "fuchsia_audio_dfx_get_num_effects");
    fn_get_info_ = (bool (*)(uint32_t, fuchsia_audio_dfx_description*))dlsym(
        dfx_lib_, "fuchsia_audio_dfx_get_info");
    fn_get_control_info_ =
        (bool (*)(uint32_t, uint16_t, fuchsia_audio_dfx_control_description*))
            dlsym(dfx_lib_, "fuchsia_audio_dfx_get_control_info");

    fn_create_ = (uint64_t(*)(uint32_t, uint32_t, uint16_t, uint16_t))dlsym(
        dfx_lib_, "fuchsia_audio_dfx_create");
    fn_delete_ =
        (bool (*)(uint64_t))dlsym(dfx_lib_, "fuchsia_audio_dfx_delete");
    fn_get_parameters_ =
        (bool (*)(uint64_t, fuchsia_audio_dfx_parameters*))dlsym(
            dfx_lib_, "fuchsia_audio_dfx_get_parameters");

    fn_get_control_value_ = (bool (*)(uint64_t, uint16_t, float*))dlsym(
        dfx_lib_, "fuchsia_audio_dfx_get_control_value");
    fn_set_control_value_ = (bool (*)(uint64_t, uint16_t, float))dlsym(
        dfx_lib_, "fuchsia_audio_dfx_set_control_value");
    fn_reset_ = (bool (*)(uint64_t))dlsym(dfx_lib_, "fuchsia_audio_dfx_reset");

    fn_process_inplace_ = (bool (*)(uint64_t, uint32_t, float*))dlsym(
        dfx_lib_, "fuchsia_audio_dfx_process_inplace");
    fn_process_ = (bool (*)(uint64_t, uint32_t, const float*, float*))dlsym(
        dfx_lib_, "fuchsia_audio_dfx_process");
    fn_flush_ = (bool (*)(uint64_t))dlsym(dfx_lib_, "fuchsia_audio_dfx_flush");

    ASSERT_NE(fn_get_num_fx_, nullptr) << "get_num_effects() did not load";
    ASSERT_NE(fn_get_info_, nullptr) << "get_info() did not load";
    ASSERT_NE(fn_get_control_info_, nullptr)
        << "get_control_info() did not load";

    ASSERT_NE(fn_create_, nullptr) << "create() did not load";
    ASSERT_NE(fn_delete_, nullptr) << "delete() did not load";
    ASSERT_NE(fn_get_parameters_, nullptr) << "get_parameters() did not load ";

    ASSERT_NE(fn_get_control_value_, nullptr)
        << "get_control_value() did not load";
    ASSERT_NE(fn_set_control_value_, nullptr)
        << "set_control_value() did not load";
    ASSERT_NE(fn_reset_, nullptr) << "reset() did not load";

    ASSERT_NE(fn_process_inplace_, nullptr)
        << "process_inplace() did not load ";
    ASSERT_NE(fn_process_, nullptr) << " process() did not load ";
    ASSERT_NE(fn_flush_, nullptr) << " flush() did not load ";
  }

  void* dfx_lib_ = nullptr;

  bool (*fn_get_num_fx_)(uint32_t*);
  bool (*fn_get_info_)(uint32_t, fuchsia_audio_dfx_description*);
  bool (*fn_get_control_info_)(uint32_t, uint16_t,
                               fuchsia_audio_dfx_control_description*);

  uint64_t (*fn_create_)(uint32_t, uint32_t, uint16_t, uint16_t);
  bool (*fn_delete_)(uint64_t);
  bool (*fn_get_parameters_)(uint64_t, fuchsia_audio_dfx_parameters*);

  bool (*fn_get_control_value_)(uint64_t, uint16_t, float*);
  bool (*fn_set_control_value_)(uint64_t, uint16_t, float);
  bool (*fn_reset_)(uint64_t);

  bool (*fn_process_inplace_)(uint64_t, uint32_t, float*);
  bool (*fn_process_)(uint64_t, uint32_t, const float*, float*);
  bool (*fn_flush_)(uint64_t);
};

// When validating controls, we make certain assumptions about the test effects.
static_assert(DfxDelay::kNumControls > 0, "DfxDelay must have controls");
static_assert(DfxRechannel::kNumControls == 0, "DFX must have no controls");
static_assert(DfxSwap::kNumControls == 0, "DfxSwap must have no controls");

// We test the delay effect with certain control values, making assumptions
// about how those values relate to the allowed range for this DFX.
constexpr float kTestDelay1 = 1.0f;
constexpr float kTestDelay2 = 2.0f;
static_assert(DfxDelay::kMaxDelayFrames >= kTestDelay2, "Test value too high");
static_assert(DfxDelay::kMinDelayFrames <= kTestDelay1, "Test value too low");
static_assert(DfxDelay::kInitialDelayFrames != kTestDelay1,
              "kTestDelay1 must not equal kInitialDelayFrames");
static_assert(DfxDelay::kInitialDelayFrames != kTestDelay2,
              "kTestDelay2 must not equal kInitialDelayFrames");

// For the most part, the below tests use a specific channel_count.
constexpr uint16_t kTestChans = 2;

// When testing or using the delay effect, we make certain channel assumptions.
static_assert(DfxDelay::kNumChannelsIn == kTestChans ||
                  DfxDelay::kNumChannelsIn == FUCHSIA_AUDIO_DFX_CHANNELS_ANY,
              "DfxDelay::kNumChannelsIn must match kTestChans");
static_assert(DfxDelay::kNumChannelsOut == kTestChans ||
                  DfxDelay::kNumChannelsOut == FUCHSIA_AUDIO_DFX_CHANNELS_ANY ||
                  DfxDelay::kNumChannelsOut ==
                      FUCHSIA_AUDIO_DFX_CHANNELS_SAME_AS_IN,
              "DfxDelay::kNumChannelsOut must match kTestChans");

// When testing or using rechannel effect, we make certain channel assumptions.
static_assert(DfxRechannel::kNumChannelsIn != 2 ||
                  DfxRechannel::kNumChannelsOut != 2,
              "DfxRechannel must not be stereo-in/-out");
static_assert(DfxRechannel::kNumChannelsIn != DfxRechannel::kNumChannelsOut &&
                  DfxRechannel::kNumChannelsOut !=
                      FUCHSIA_AUDIO_DFX_CHANNELS_ANY &&
                  DfxRechannel::kNumChannelsOut !=
                      FUCHSIA_AUDIO_DFX_CHANNELS_SAME_AS_IN,
              "DfxRechannel must not be in-place");

// When testing or using the swap effect, we make certain channel assumptions.
static_assert(DfxSwap::kNumChannelsIn == kTestChans ||
                  DfxSwap::kNumChannelsIn == FUCHSIA_AUDIO_DFX_CHANNELS_ANY,
              "DfxSwap::kNumChannelsIn must match kTestChans");
static_assert(DfxSwap::kNumChannelsOut == kTestChans ||
                  DfxSwap::kNumChannelsOut == FUCHSIA_AUDIO_DFX_CHANNELS_ANY ||
                  DfxSwap::kNumChannelsOut ==
                      FUCHSIA_AUDIO_DFX_CHANNELS_SAME_AS_IN,
              "DfxSwap::kNumChannelsOut must match kTestChans");

// Tests the get_num_effects ABI, and that the test library behaves as expected.
TEST_F(DfxBaseTest, GetNumEffects) {
  uint32_t num_effects;

  EXPECT_TRUE(fn_get_num_fx_(&num_effects));
  EXPECT_TRUE(num_effects == DfxBase::kNumTestEffects);

  // Verify null out_param
  EXPECT_FALSE(fn_get_num_fx_(nullptr));
}

// Tests the get_info ABI, and that the test DFXs behave as expected.
TEST_F(DfxBaseTest, GetInfo) {
  fuchsia_audio_dfx_description dfx_desc;

  EXPECT_TRUE(fn_get_info_(Effect::Delay, &dfx_desc));
  EXPECT_TRUE(dfx_desc.num_controls == DfxDelay::kNumControls);
  EXPECT_TRUE(dfx_desc.incoming_channels == DfxDelay::kNumChannelsIn);
  EXPECT_TRUE(dfx_desc.outgoing_channels == DfxDelay::kNumChannelsOut);

  EXPECT_TRUE(fn_get_info_(Effect::Swap, &dfx_desc));
  EXPECT_TRUE(dfx_desc.num_controls == DfxSwap::kNumControls);
  EXPECT_TRUE(dfx_desc.incoming_channels == DfxSwap::kNumChannelsIn);
  EXPECT_TRUE(dfx_desc.outgoing_channels == DfxSwap::kNumChannelsOut);

  EXPECT_TRUE(fn_get_info_(Effect::Rechannel, &dfx_desc));
  EXPECT_TRUE(dfx_desc.num_controls == DfxRechannel::kNumControls);
  EXPECT_TRUE(dfx_desc.incoming_channels == DfxRechannel::kNumChannelsIn);
  EXPECT_TRUE(dfx_desc.outgoing_channels == DfxRechannel::kNumChannelsOut);

  // Verify effect beyond range
  EXPECT_FALSE(fn_get_info_(Effect::Count, &dfx_desc));
  // Verify null struct*
  EXPECT_FALSE(fn_get_info_(Effect::Rechannel, nullptr));
}

// Tests the get_control_info ABI, and that the test DFXs behave as expected.
TEST_F(DfxBaseTest, GetControlInfo) {
  fuchsia_audio_dfx_control_description dfx_control_desc;

  EXPECT_TRUE(fn_get_control_info_(Effect::Delay, 0, &dfx_control_desc));
  EXPECT_LE(dfx_control_desc.initial_val, dfx_control_desc.max_val);
  EXPECT_GE(dfx_control_desc.initial_val, dfx_control_desc.min_val);
  EXPECT_TRUE(dfx_control_desc.max_val == DfxDelay::kMaxDelayFrames);
  EXPECT_TRUE(dfx_control_desc.min_val == DfxDelay::kMinDelayFrames);
  EXPECT_TRUE(dfx_control_desc.initial_val == DfxDelay::kInitialDelayFrames);

  // Verify control beyond range
  EXPECT_FALSE(fn_get_control_info_(Effect::Delay, DfxDelay::kNumControls,
                                    &dfx_control_desc));
  // Verify null struct*
  EXPECT_FALSE(fn_get_control_info_(Effect::Delay, 0, nullptr));

  // Verify effects with no controls
  EXPECT_FALSE(fn_get_control_info_(Effect::Rechannel, 0, &dfx_control_desc));
  EXPECT_FALSE(fn_get_control_info_(Effect::Swap, 0, &dfx_control_desc));
  EXPECT_FALSE(fn_get_control_info_(Effect::Count, 0, &dfx_control_desc));
}

// Tests the create ABI.
TEST_F(DfxBaseTest, Create) {
  uint32_t frame_rate = 0;
  uint64_t dfx_token =
      fn_create_(Effect::Delay, frame_rate, kTestChans, kTestChans);
  EXPECT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  dfx_token = fn_create_(Effect::Swap, frame_rate, kTestChans, kTestChans);
  EXPECT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  dfx_token =
      fn_create_(Effect::Rechannel, frame_rate, DfxRechannel::kNumChannelsIn,
                 DfxRechannel::kNumChannelsOut);
  EXPECT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  // Verify num_channels mismatch (is not equal, should be)
  EXPECT_EQ(fn_create_(Effect::Delay, frame_rate, kTestChans, kTestChans - 1),
            FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  // Verify num_channels mismatch (is equal, should not be)
  EXPECT_EQ(fn_create_(Effect::Rechannel, frame_rate, kTestChans, kTestChans),
            FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  // Verify effect out of range
  EXPECT_EQ(fn_create_(Effect::Count, frame_rate, kTestChans, kTestChans),
            FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  // Verify channels out of range
  EXPECT_EQ(
      fn_create_(Effect::Delay, frame_rate, FUCHSIA_AUDIO_DFX_CHANNELS_MAX + 1,
                 FUCHSIA_AUDIO_DFX_CHANNELS_MAX + 1),
      FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  EXPECT_EQ(fn_create_(Effect::Delay, frame_rate,
                       FUCHSIA_AUDIO_DFX_CHANNELS_SAME_AS_IN,
                       FUCHSIA_AUDIO_DFX_CHANNELS_SAME_AS_IN),
            FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
}

// Tests the delete ABI.
TEST_F(DfxBaseTest, Delete) {
  uint64_t dfx_token = fn_create_(Effect::Delay, 48000, kTestChans, kTestChans);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  EXPECT_TRUE(fn_delete_(dfx_token));
  EXPECT_FALSE(fn_delete_(FUCHSIA_AUDIO_DFX_INVALID_TOKEN));
}

// Tests the get_parameters ABI, and that the test DFX behaves as expected.
TEST_F(DfxBaseTest, GetParameters_Delay) {
  fuchsia_audio_dfx_parameters device_fx_params;

  uint32_t frame_rate = 48000;
  uint64_t dfx_token =
      fn_create_(Effect::Delay, frame_rate, kTestChans, kTestChans);

  EXPECT_TRUE(fn_get_parameters_(dfx_token, &device_fx_params));
  EXPECT_EQ(device_fx_params.frame_rate, frame_rate);
  EXPECT_EQ(device_fx_params.channels_in, kTestChans);
  EXPECT_EQ(device_fx_params.channels_out, kTestChans);
  EXPECT_TRUE(device_fx_params.signal_latency_frames ==
              DfxDelay::kLatencyFrames);
  EXPECT_TRUE(device_fx_params.suggested_frames_per_buffer ==
              DfxDelay::kLatencyFrames);

  // Verify invalid device token
  EXPECT_FALSE(
      fn_get_parameters_(FUCHSIA_AUDIO_DFX_INVALID_TOKEN, &device_fx_params));

  // Verify null struct*
  EXPECT_FALSE(fn_get_parameters_(dfx_token, nullptr));

  EXPECT_TRUE(fn_delete_(dfx_token));
}

// Tests the get_parameters ABI, and that the test DFX behaves as expected.
TEST_F(DfxBaseTest, GetParameters_Rechannel) {
  fuchsia_audio_dfx_parameters device_fx_params;

  uint32_t frame_rate = 48000;
  uint64_t dfx_token =
      fn_create_(Effect::Rechannel, frame_rate, DfxRechannel::kNumChannelsIn,
                 DfxRechannel::kNumChannelsOut);
  device_fx_params.frame_rate = 44100;  // should be overwritten

  EXPECT_TRUE(fn_get_parameters_(dfx_token, &device_fx_params));
  EXPECT_EQ(device_fx_params.frame_rate, frame_rate);
  EXPECT_TRUE(device_fx_params.channels_in == DfxRechannel::kNumChannelsIn);
  EXPECT_TRUE(device_fx_params.channels_out == DfxRechannel::kNumChannelsOut);
  EXPECT_TRUE(device_fx_params.signal_latency_frames ==
              DfxRechannel::kLatencyFrames);
  EXPECT_TRUE(device_fx_params.suggested_frames_per_buffer ==
              DfxRechannel::kLatencyFrames);
  EXPECT_TRUE(fn_delete_(dfx_token));
}

// Tests the get_parameters ABI, and that the test DFX behaves as expected.
TEST_F(DfxBaseTest, GetParameters_Swap) {
  fuchsia_audio_dfx_parameters device_fx_params;

  uint32_t frame_rate = 44100;
  uint64_t dfx_token =
      fn_create_(Effect::Swap, frame_rate, kTestChans, kTestChans);
  device_fx_params.frame_rate = 48000;  // should be overwritten

  EXPECT_TRUE(fn_get_parameters_(dfx_token, &device_fx_params));
  EXPECT_EQ(device_fx_params.frame_rate, frame_rate);
  EXPECT_EQ(device_fx_params.channels_in, kTestChans);
  EXPECT_EQ(device_fx_params.channels_out, kTestChans);
  EXPECT_TRUE(device_fx_params.signal_latency_frames ==
              DfxSwap::kLatencyFrames);
  EXPECT_TRUE(device_fx_params.suggested_frames_per_buffer ==
              DfxSwap::kLatencyFrames);
  EXPECT_TRUE(fn_delete_(dfx_token));
}

// Tests the get_control_value ABI, and that the test DFX behaves as expected.
TEST_F(DfxBaseTest, GetControlValue_Delay) {
  uint16_t control_num = 0;
  fuchsia_audio_dfx_description dfx_desc;
  fuchsia_audio_dfx_control_description dfx_control_desc;

  ASSERT_TRUE(fn_get_info_(Effect::Delay, &dfx_desc));
  ASSERT_GT(dfx_desc.num_controls, control_num);
  ASSERT_TRUE(
      fn_get_control_info_(Effect::Delay, control_num, &dfx_control_desc));

  uint64_t dfx_token = fn_create_(Effect::Delay, 48000, kTestChans, kTestChans);
  ASSERT_TRUE(dfx_token);
  float val;
  EXPECT_TRUE(fn_get_control_value_(dfx_token, control_num, &val));

  EXPECT_GE(val, dfx_control_desc.min_val);
  EXPECT_LE(val, dfx_control_desc.max_val);
  EXPECT_EQ(val, dfx_control_desc.initial_val);

  // Verify invalid effect token
  EXPECT_FALSE(fn_get_control_value_(FUCHSIA_AUDIO_DFX_INVALID_TOKEN,
                                     control_num, &val));
  // Verify control out of range
  EXPECT_FALSE(fn_get_control_value_(dfx_token, dfx_desc.num_controls, &val));
  // Verify null out_param
  EXPECT_FALSE(fn_get_control_value_(dfx_token, control_num, nullptr));
  EXPECT_TRUE(fn_delete_(dfx_token));
}

// Tests cases in which we expect get_control_value to fail.
TEST_F(DfxBaseTest, GetControlValue_Other) {
  float val;

  uint64_t dfx_token =
      fn_create_(Effect::Rechannel, 48000, DfxRechannel::kNumChannelsIn,
                 DfxRechannel::kNumChannelsOut);
  ASSERT_TRUE(dfx_token);
  EXPECT_FALSE(fn_get_control_value_(dfx_token, 0, &val));
  EXPECT_TRUE(fn_delete_(dfx_token));

  dfx_token = fn_create_(Effect::Swap, 48000, kTestChans, kTestChans);
  ASSERT_TRUE(dfx_token);
  EXPECT_FALSE(fn_get_control_value_(dfx_token, 0, &val));
  EXPECT_TRUE(fn_delete_(dfx_token));
}

// Tests the set_control_value ABI, and that the test DFX behaves as expected.
TEST_F(DfxBaseTest, SetControlValue_Delay) {
  uint16_t control_num = 0;
  fuchsia_audio_dfx_description dfx_desc;
  fuchsia_audio_dfx_control_description dfx_control_desc;

  ASSERT_TRUE(fn_get_info_(Effect::Delay, &dfx_desc));
  ASSERT_GT(dfx_desc.num_controls, control_num);
  ASSERT_TRUE(
      fn_get_control_info_(Effect::Delay, control_num, &dfx_control_desc));

  uint64_t dfx_token = fn_create_(Effect::Delay, 48000, kTestChans, kTestChans);

  EXPECT_TRUE(fn_set_control_value_(dfx_token, control_num, kTestDelay1));

  float new_value;
  EXPECT_TRUE(fn_get_control_value_(dfx_token, control_num, &new_value));
  EXPECT_EQ(new_value, kTestDelay1);

  // Verify invalid effect token
  EXPECT_FALSE(fn_set_control_value_(FUCHSIA_AUDIO_DFX_INVALID_TOKEN,
                                     control_num, kTestDelay1));
  // Verify control out of range
  EXPECT_FALSE(
      fn_set_control_value_(dfx_token, dfx_desc.num_controls, kTestDelay1));
  // Verify value out of range
  EXPECT_FALSE(fn_set_control_value_(dfx_token, control_num,
                                     dfx_control_desc.max_val + 1));
  EXPECT_TRUE(fn_delete_(dfx_token));
}

// Tests cases in which we expect set_control_value to fail.
TEST_F(DfxBaseTest, SetControlValue_Other) {
  uint64_t dfx_token =
      fn_create_(Effect::Rechannel, 48000, DfxRechannel::kNumChannelsIn,
                 DfxRechannel::kNumChannelsOut);
  ASSERT_TRUE(dfx_token);
  EXPECT_FALSE(fn_set_control_value_(dfx_token, 0, 0));
  EXPECT_TRUE(fn_delete_(dfx_token));

  dfx_token = fn_create_(Effect::Swap, 48000, kTestChans, kTestChans);
  ASSERT_TRUE(dfx_token);
  EXPECT_FALSE(fn_set_control_value_(dfx_token, 0, 0));
  EXPECT_TRUE(fn_delete_(dfx_token));
}

// Tests the reset ABI, and that DFX discards state and control values.
TEST_F(DfxBaseTest, Reset) {
  uint16_t control_num = 0;
  fuchsia_audio_dfx_description dfx_desc;
  fuchsia_audio_dfx_control_description dfx_control_desc;

  ASSERT_TRUE(fn_get_info_(Effect::Delay, &dfx_desc));
  ASSERT_GT(dfx_desc.num_controls, control_num);
  ASSERT_TRUE(
      fn_get_control_info_(Effect::Delay, control_num, &dfx_control_desc));

  uint64_t dfx_token = fn_create_(Effect::Delay, 48000, kTestChans, kTestChans);

  float new_value;
  ASSERT_TRUE(fn_get_control_value_(dfx_token, control_num, &new_value));
  EXPECT_NE(new_value, kTestDelay1);

  ASSERT_TRUE(fn_set_control_value_(dfx_token, control_num, kTestDelay1));
  ASSERT_TRUE(fn_get_control_value_(dfx_token, control_num, &new_value));
  ASSERT_EQ(new_value, kTestDelay1);

  EXPECT_TRUE(fn_reset_(dfx_token));
  EXPECT_TRUE(fn_get_control_value_(dfx_token, control_num, &new_value));
  EXPECT_NE(new_value, kTestDelay1);
  EXPECT_TRUE(new_value == DfxDelay::kInitialDelayFrames);
  EXPECT_TRUE(fn_delete_(dfx_token));

  // Verify invalid effect token
  EXPECT_FALSE(fn_reset_(FUCHSIA_AUDIO_DFX_INVALID_TOKEN));
}

// Tests the process_inplace ABI, and that the test DFX behaves as expected.
TEST_F(DfxBaseTest, ProcessInPlace_Delay) {
  constexpr uint32_t kNumFrames = 12;
  constexpr uint32_t kDelayFrames = 6;
  float delay_buff_in_out[kNumFrames * kTestChans] = {0};
  float expect[kNumFrames * kTestChans];
  for (uint32_t i = 0; i < kNumFrames; ++i) {
    delay_buff_in_out[i * kTestChans] = static_cast<float>(i + 1);
    delay_buff_in_out[i * kTestChans + 1] = -static_cast<float>(i + 1);

    expect[i * kTestChans] =
        (i < kDelayFrames ? 0.0f
                          : delay_buff_in_out[(i - kDelayFrames) * kTestChans]);
    expect[i * kTestChans + 1] =
        (i < kDelayFrames
             ? 0.0f
             : delay_buff_in_out[(i - kDelayFrames) * kTestChans + 1]);
  }

  uint64_t dfx_token = fn_create_(Effect::Delay, 48000, kTestChans, kTestChans);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  uint16_t control_num = 0;
  ASSERT_TRUE(fn_set_control_value_(dfx_token, control_num,
                                    static_cast<float>(kDelayFrames)));

  EXPECT_TRUE(fn_process_inplace_(dfx_token, 4, delay_buff_in_out));
  EXPECT_TRUE(
      fn_process_inplace_(dfx_token, 4, delay_buff_in_out + (4 * kTestChans)));
  EXPECT_TRUE(
      fn_process_inplace_(dfx_token, 4, delay_buff_in_out + (8 * kTestChans)));

  for (uint32_t sample = 0; sample < kNumFrames * kTestChans; ++sample) {
    EXPECT_EQ(delay_buff_in_out[sample], expect[sample]);
  }
  EXPECT_TRUE(fn_process_inplace_(dfx_token, 0, delay_buff_in_out));
  EXPECT_TRUE(fn_delete_(dfx_token));
}

// Tests the process_inplace ABI, and that the test DFX behaves as expected.
TEST_F(DfxBaseTest, ProcessInPlace_Swap) {
  constexpr uint32_t kNumFrames = 4;
  float right_left_buff_in_out[kNumFrames * kTestChans] = {
      1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f};

  uint64_t dfx_token = fn_create_(Effect::Swap, 48000, kTestChans, kTestChans);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  EXPECT_TRUE(
      fn_process_inplace_(dfx_token, kNumFrames, right_left_buff_in_out));
  for (uint32_t sample_num = 0; sample_num < kNumFrames * kTestChans;
       ++sample_num) {
    EXPECT_EQ(right_left_buff_in_out[sample_num],
              (sample_num % 2 ? 1.0f : -1.0f));
  }

  EXPECT_TRUE(fn_process_inplace_(dfx_token, 0, right_left_buff_in_out));
  EXPECT_TRUE(fn_delete_(dfx_token));
}

// Tests cases in which we expect process_inplace to fail.
TEST_F(DfxBaseTest, ProcessInPlace_Fail) {
  constexpr uint32_t kNumFrames = 1;
  float buff_in_out[kNumFrames * DfxRechannel::kNumChannelsIn] = {0};

  uint64_t dfx_token = fn_create_(Effect::Swap, 48000, kTestChans, kTestChans);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  // Calls with invalid token or null buff_ptr should fail.
  EXPECT_FALSE(fn_process_inplace_(FUCHSIA_AUDIO_DFX_INVALID_TOKEN, kNumFrames,
                                   buff_in_out));
  EXPECT_FALSE(fn_process_inplace_(dfx_token, kNumFrames, nullptr));
  EXPECT_FALSE(fn_process_inplace_(dfx_token, 0, nullptr));
  EXPECT_TRUE(fn_delete_(dfx_token));

  // Effects that change the channelization should not process in-place.
  dfx_token = fn_create_(Effect::Rechannel, 48000, DfxRechannel::kNumChannelsIn,
                         DfxRechannel::kNumChannelsOut);
  ASSERT_TRUE(dfx_token);
  EXPECT_FALSE(fn_process_inplace_(dfx_token, kNumFrames, buff_in_out));
  EXPECT_TRUE(fn_delete_(dfx_token));
}

// Tests the process ABI, and that the test DFX behaves as expected.
TEST_F(DfxBaseTest, Process_Rechannel) {
  constexpr uint32_t kNumFrames = 1;
  float audio_buff_in[kNumFrames * DfxRechannel::kNumChannelsIn] = {
      1.0f, -1.0f, 0.25f, -1.0f, 0.98765432f, -0.09876544f};
  float audio_buff_out[kNumFrames * DfxRechannel::kNumChannelsOut] = {0.0f};
  float expected[kNumFrames * DfxRechannel::kNumChannelsOut] = {0.799536645f,
                                                                -0.340580851f};

  uint64_t dfx_token =
      fn_create_(Effect::Rechannel, 48000, DfxRechannel::kNumChannelsIn,
                 DfxRechannel::kNumChannelsOut);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  EXPECT_TRUE(
      fn_process_(dfx_token, kNumFrames, audio_buff_in, audio_buff_out));
  EXPECT_EQ(audio_buff_out[0], expected[0])
      << std::setprecision(9) << audio_buff_out[0];
  EXPECT_EQ(audio_buff_out[1], expected[1])
      << std::setprecision(9) << audio_buff_out[1];

  EXPECT_TRUE(fn_process_(dfx_token, 0, audio_buff_in, audio_buff_out));
  EXPECT_TRUE(fn_delete_(dfx_token));
}

// Tests cases in which we expect process to fail.
TEST_F(DfxBaseTest, Process_Fail) {
  constexpr uint32_t kNumFrames = 1;
  float audio_buff_in[kNumFrames * kTestChans] = {0.0f};
  float audio_buff_out[kNumFrames * kTestChans] = {0.0f};

  // Test null token, buffer_in, buffer_out
  uint64_t dfx_token =
      fn_create_(Effect::Rechannel, 48000, DfxRechannel::kNumChannelsIn,
                 DfxRechannel::kNumChannelsOut);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
  EXPECT_FALSE(fn_process_(FUCHSIA_AUDIO_DFX_INVALID_TOKEN, kNumFrames,
                           audio_buff_in, audio_buff_out));
  EXPECT_FALSE(fn_process_(dfx_token, kNumFrames, nullptr, audio_buff_out));
  EXPECT_FALSE(fn_process_(dfx_token, kNumFrames, audio_buff_in, nullptr));
  EXPECT_FALSE(fn_process_(dfx_token, 0, nullptr, audio_buff_out));
  EXPECT_FALSE(fn_process_(dfx_token, 0, audio_buff_in, nullptr));
  EXPECT_TRUE(fn_delete_(dfx_token));

  // These stereo-to-stereo effects should ONLY process in-place
  dfx_token = fn_create_(Effect::Swap, 48000, kTestChans, kTestChans);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
  EXPECT_FALSE(
      fn_process_(dfx_token, kNumFrames, audio_buff_in, audio_buff_out));
  EXPECT_TRUE(fn_delete_(dfx_token));

  dfx_token = fn_create_(Effect::Delay, 48000, kTestChans, kTestChans);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
  EXPECT_FALSE(
      fn_process_(dfx_token, kNumFrames, audio_buff_in, audio_buff_out));
  EXPECT_TRUE(fn_delete_(dfx_token));
}

// Tests the process_inplace ABI thru successive in-place calls.
TEST_F(DfxBaseTest, ProcessInPlace_Chain) {
  constexpr uint32_t kNumFrames = 6;

  float buff_in_out[kNumFrames * kTestChans] = {1.0f, -0.1f, -0.2f, 2.0f,
                                                0.3f, -3.0f, -4.0f, 0.4f,
                                                5.0f, -0.5f, -0.6f, 6.0f};
  float expected[kNumFrames * kTestChans] = {0.0f, 0.0f,  0.0f,  0.0f,
                                             0.0f, 0.0f,  -0.1f, 1.0f,
                                             2.0f, -0.2f, -3.0f, 0.3f};

  uint64_t delay1_token, swap_token, delay2_token;
  delay1_token = fn_create_(Effect::Delay, 44100, kTestChans, kTestChans);
  swap_token = fn_create_(Effect::Swap, 44100, kTestChans, kTestChans);
  delay2_token = fn_create_(Effect::Delay, 44100, kTestChans, kTestChans);

  ASSERT_NE(delay1_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
  ASSERT_NE(swap_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
  ASSERT_NE(delay2_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  uint16_t control_num = 0;
  ASSERT_TRUE(fn_set_control_value_(delay1_token, control_num, kTestDelay1));
  ASSERT_TRUE(fn_set_control_value_(delay2_token, control_num, kTestDelay2));

  EXPECT_TRUE(fn_process_inplace_(delay1_token, kNumFrames, buff_in_out));
  EXPECT_TRUE(fn_process_inplace_(swap_token, kNumFrames, buff_in_out));
  EXPECT_TRUE(fn_process_inplace_(delay2_token, kNumFrames, buff_in_out));
  for (uint32_t sample_num = 0; sample_num < kNumFrames * kTestChans;
       ++sample_num) {
    EXPECT_EQ(buff_in_out[sample_num], expected[sample_num]) << sample_num;
  }

  EXPECT_TRUE(fn_process_inplace_(delay2_token, 0, buff_in_out));
  EXPECT_TRUE(fn_process_inplace_(swap_token, 0, buff_in_out));
  EXPECT_TRUE(fn_process_inplace_(delay1_token, 0, buff_in_out));

  EXPECT_TRUE(fn_delete_(delay2_token));
  EXPECT_TRUE(fn_delete_(swap_token));
  EXPECT_TRUE(fn_delete_(delay1_token));
}

// Tests the flush ABI, and that DFX discards state but retains control values.
TEST_F(DfxBaseTest, Flush) {
  constexpr uint32_t kNumFrames = 1;
  float buff_in_out[kTestChans] = {1.0f, -1.0f};

  uint64_t dfx_token = fn_create_(Effect::Delay, 44100, kTestChans, kTestChans);

  float new_value;
  ASSERT_TRUE(fn_get_control_value_(dfx_token, 0, &new_value));
  EXPECT_NE(new_value, kTestDelay1);

  ASSERT_TRUE(fn_set_control_value_(dfx_token, 0, kTestDelay1));
  ASSERT_TRUE(fn_get_control_value_(dfx_token, 0, &new_value));
  ASSERT_EQ(new_value, kTestDelay1);

  ASSERT_TRUE(fn_process_inplace_(dfx_token, kNumFrames, buff_in_out));
  ASSERT_EQ(buff_in_out[0], 0.0f);

  EXPECT_TRUE(fn_flush_(dfx_token));

  // Validate that settings are retained after Flush.
  EXPECT_TRUE(fn_get_control_value_(dfx_token, 0, &new_value));
  EXPECT_EQ(new_value, kTestDelay1);

  // Validate that cached samples are flushed.
  EXPECT_TRUE(fn_process_inplace_(dfx_token, kNumFrames, buff_in_out));
  EXPECT_EQ(buff_in_out[0], 0.0f);

  // Verify invalid effect token
  EXPECT_FALSE(fn_flush_(FUCHSIA_AUDIO_DFX_INVALID_TOKEN));
  EXPECT_TRUE(fn_delete_(dfx_token));
}

}  // namespace audio_dfx_test
}  // namespace media
