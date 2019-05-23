// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <unittest/unittest.h>

namespace audio {

class MockSimpleAudio : public SimpleAudioStream {

public:
    MockSimpleAudio(zx_device_t* parent)
    : SimpleAudioStream(parent, true /* is input */) {}

protected:
    zx_status_t Init() __TA_REQUIRES(domain_->token()) override {

        fbl::AllocChecker ac;
        supported_formats_.reserve(1, &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        audio_stream_format_range_t range;

        range.min_channels = 2;
        range.max_channels = 2;
        range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
        range.min_frames_per_second = 48000;
        range.max_frames_per_second = 48000;
        range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;

        supported_formats_.push_back(range);

        // Set our gain capabilities.
        cur_gain_state_.cur_gain = 0;
        cur_gain_state_.cur_mute = false;
        cur_gain_state_.cur_agc = false;
        cur_gain_state_.min_gain = 0;
        cur_gain_state_.max_gain = 0;
        cur_gain_state_.gain_step = 0;
        cur_gain_state_.can_mute = false;
        cur_gain_state_.can_agc = false;

        snprintf(device_name_, sizeof(device_name_), "test-audio-in");
        snprintf(mfr_name_, sizeof(mfr_name_), "Bike Sheds, Inc.");
        snprintf(prod_name_, sizeof(prod_name_), "testy_mctestface");

        unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_MICROPHONE;

        return ZX_OK;
    }

    zx_status_t ChangeFormat(const audio_proto::StreamSetFmtReq& req)
        __TA_REQUIRES(domain_->token()) override {

        return ZX_OK;
    }

    zx_status_t GetBuffer(const audio_proto::RingBufGetBufferReq& req,
                          uint32_t* out_num_rb_frames,
                          zx::vmo* out_buffer) __TA_REQUIRES(domain_->token()) override {
        return ZX_OK;
    }

    zx_status_t Start(uint64_t* out_start_time) __TA_REQUIRES(domain_->token()) override {
        *out_start_time = zx_clock_get_monotonic();
        return ZX_OK;
    }

    zx_status_t Stop() __TA_REQUIRES(domain_->token()) override {
        return ZX_OK;
    }

    void ShutdownHook() __TA_REQUIRES(domain_->token()) override {
        Stop();
    }
};

class Bind;
class Bind : public fake_ddk::Bind {
public:
    int total_children() const { return total_children_; }

    zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                          zx_device_t** out) override {
        if (parent == fake_ddk::kFakeParent) {
            *out = fake_ddk::kFakeDevice;
            add_called_ = true;
        } else if (parent == fake_ddk::kFakeDevice) {
            *out = kFakeChild;
            children_++;
            total_children_++;
        } else {
            *out = kUnknownDevice;
            bad_parent_ = false;
        }
        return ZX_OK;
    }

    zx_status_t DeviceRemove(zx_device_t* device) override {
        if (device == fake_ddk::kFakeDevice) {
            remove_called_ = true;
        } else if (device == kFakeChild) {
            // Check that all children are removed before the parent is removed.
            if (!remove_called_) {
                children_--;
            }
        } else {
            bad_device_ = true;
        }
        return ZX_OK;
    }

    bool IsRemoved() { return remove_called_; }

    bool Ok() {
        return ((children_ == 0) && add_called_ && remove_called_ &&
               !bad_parent_ && !bad_device_);
    }
private:
    zx_device_t* kFakeChild = reinterpret_cast<zx_device_t*>(0x1234);
    zx_device_t* kUnknownDevice = reinterpret_cast<zx_device_t*>(0x5678);

    int total_children_ = 0;
    int children_ = 0;

    bool bad_parent_ = false;
    bool bad_device_ = false;
    bool add_called_ = false;
    bool remove_called_ = false;
};

bool DdkLifeCycleTest() {
    BEGIN_TEST;
    Bind tester;
    auto stream =
        audio::SimpleAudioStream::Create<audio::MockSimpleAudio>(fake_ddk::kFakeParent);
    ASSERT_NOT_NULL(stream);
    ASSERT_EQ(ZX_OK, stream->DdkSuspend(0));
    EXPECT_FALSE(tester.IsRemoved());
    stream->DdkUnbind();
    EXPECT_TRUE(tester.Ok());
    END_TEST;
}
} //namespace audio

BEGIN_TEST_CASE(SimpleAudioTest)
RUN_TEST_SMALL(audio::DdkLifeCycleTest)
END_TEST_CASE(SimpleAudioTest)
