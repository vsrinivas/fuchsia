// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/audio2.h>
#include <magenta/types.h>
#include <mx/channel.h>
#include <mx/vmo.h>
#include <mxtl/unique_ptr.h>

class AudioStream {
public:
    static mxtl::unique_ptr<AudioStream> Create(bool input, uint32_t dev_id);

    mx_status_t Open();
    mx_status_t DumpInfo();
    mx_status_t SetMute(bool mute);
    mx_status_t SetGain(float gain);
    mx_status_t PlugMonitor(float duration);

    const char* name()  const { return name_; }
    bool        input() const { return input_; }

protected:
    friend class mxtl::unique_ptr<AudioStream>;

    mx_status_t GetPlugState(audio2_stream_cmd_plug_detect_resp_t* out_state,
                             bool enable_notify = false);
    void        DisablePlugNotifications();
    mx_status_t SetFormat(uint32_t frames_per_second,
                          uint16_t channels,
                          audio2_sample_format_t sample_format);
    mx_status_t GetBuffer(uint32_t frames, uint32_t irqs_per_ring);
    mx_status_t StartRingBuffer();
    mx_status_t StopRingBuffer();

    AudioStream(bool input, uint32_t dev_id);
    virtual ~AudioStream() { }

    mx::channel stream_ch_;
    mx::channel rb_ch_;
    mx::vmo     rb_vmo_;

    const bool     input_;
    const uint32_t dev_id_;

    char     name_[64]    = { 0 };
    uint32_t frame_rate_  = 0;
    uint32_t sample_size_ = 0;
    uint32_t channel_cnt_ = 0;
    uint32_t frame_sz_    = 0;
    uint32_t rb_sz_       = 0;
    void*    rb_virt_     = nullptr;
};
