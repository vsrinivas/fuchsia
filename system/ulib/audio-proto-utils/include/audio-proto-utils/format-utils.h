// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/assert.h>
#include <magenta/device/audio.h>
#include <magenta/types.h>
#include <string.h>

namespace audio {
namespace utils {

// Check to see if the specified frame rate is in either the 48 KHz or 44.1 KHz
// family.
bool FrameRateIn48kFamily(uint32_t rate);
bool FrameRateIn441kFamily(uint32_t rate);

// Figure out the size of an audio frame based on the sample format.  Returns 0
// in the case of an error (bad channel count, bad sample format)
uint32_t ComputeFrameSize(uint16_t channels, audio_sample_format_t sample_format);

// Check to see if the specified format (rate, chan, sample_format) is
// compatible with the given format range.  Returns true if it is, or
// false otherwise.
bool FormatIsCompatible(uint32_t frame_rate,
                        uint16_t channels,
                        audio_sample_format_t sample_format,
                        const audio_stream_format_range_t& format_range);

// A small helper class which allows code to use c++11 range-based for loop
// syntax for enumerating discrete frame rates supported by an
// audio_stream_format_range_t.  Note that this enumerator will not enumerate
// anything if the frame rate range is continuous.
//
// TODO(johngro): If/when we switch to C++17, the begin/end expressions demanded
// by the range-based for loop no longer need to return identical types.  We can
// use this to our advantage to eliminate the need to create an actual iterator
// instance when calling end().  We could just return some sort of strongly type
// enum placeholder instead and allow comparison between the iterator and the
// token.
class FrameRateEnumerator {
public:
    class iterator {
    public:
        iterator() { }

        iterator& operator++() {
            Advance();
            return *this;
        }

        uint32_t operator*() {
            // No one should be dereferencing us if we are currently invalid.
            MX_DEBUG_ASSERT(enumerator_ != nullptr);
            return cur_rate_;
        }

        bool operator!=(const iterator& rhs) const {
            return ::memcmp(this, &rhs, sizeof(*this)) != 0;
        }

    private:
        friend class FrameRateEnumerator;
        explicit iterator(const FrameRateEnumerator* enumerator);
        void Advance();

        const FrameRateEnumerator* enumerator_ = nullptr;
        uint32_t cur_rate_ = 0;
        uint16_t cur_flag_ = 0;
        uint16_t fmt_ndx_ = 0;
    };

    explicit FrameRateEnumerator(const audio_stream_format_range_t& range) : range_(range) { }

    iterator begin() { return iterator(this); }
    iterator end()   { return iterator(nullptr); }

    const audio_stream_format_range_t& range() const { return range_; }

private:
    audio_stream_format_range_t range_;
};

}  // namespace utils
}  // namespace audio
