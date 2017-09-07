// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <audio-proto-utils/format-utils.h>
#include <fbl/algorithm.h>
#include <string.h>

namespace audio {
namespace utils {

// Note: these sets must be kept in monotonically increasing order.
static const uint32_t RATES_48000_FAMILY[] = { 8000,16000,32000,48000,96000,192000,384000,768000 };
static const uint32_t RATES_44100_FAMILY[] = { 11025,22050,44100,88200,176400 };
static const uint32_t* RATES_48000_FAMILY_LAST = RATES_48000_FAMILY + fbl::count_of(RATES_48000_FAMILY);
static const uint32_t* RATES_44100_FAMILY_LAST = RATES_44100_FAMILY + fbl::count_of(RATES_44100_FAMILY);
static constexpr auto DISCRETE_FLAGS = ASF_RANGE_FLAG_FPS_48000_FAMILY
                                     | ASF_RANGE_FLAG_FPS_44100_FAMILY;

bool FrameRateIn48kFamily(uint32_t rate) {
    const uint32_t* found = fbl::lower_bound(RATES_48000_FAMILY, RATES_48000_FAMILY_LAST, rate);
    return ((found < RATES_48000_FAMILY_LAST) && (*found == rate));
}

bool FrameRateIn441kFamily(uint32_t rate) {
    const uint32_t* found = fbl::lower_bound(RATES_44100_FAMILY, RATES_44100_FAMILY_LAST, rate);
    return ((found < RATES_44100_FAMILY_LAST) && (*found == rate));
}

// Figure out the size of an audio frame based on the sample format.  Returns 0
// in the case of an error (bad channel count, bad sample format)
uint32_t ComputeFrameSize(uint16_t channels, audio_sample_format_t sample_format) {
    uint32_t fmt_noflags = sample_format & ~AUDIO_SAMPLE_FORMAT_FLAG_MASK;

    switch (fmt_noflags) {
    case AUDIO_SAMPLE_FORMAT_8BIT:         return 1u * channels;
    case AUDIO_SAMPLE_FORMAT_16BIT:        return 2u * channels;
    case AUDIO_SAMPLE_FORMAT_24BIT_PACKED: return 3u * channels;
    case AUDIO_SAMPLE_FORMAT_20BIT_IN32:
    case AUDIO_SAMPLE_FORMAT_24BIT_IN32:
    case AUDIO_SAMPLE_FORMAT_32BIT:
    case AUDIO_SAMPLE_FORMAT_32BIT_FLOAT:  return 4u * channels;

    // See MG-1003
    // We currently don't really know how 20 bit audio should be packed.  For
    // now, treat it as an error.
    case AUDIO_SAMPLE_FORMAT_20BIT_PACKED:
    default:
        return 0;
    }
}

bool FormatIsCompatible(uint32_t frame_rate,
                        uint16_t channels,
                        audio_sample_format_t sample_format,
                        const audio_stream_format_range_t& format_range) {
    // Are the requested number of channels in range?
    if ((channels < format_range.min_channels) || (channels > format_range.max_channels))
        return false;

    // Is the requested sample format compatible with the range's supported
    // formats?  If so...
    //
    // 1) The flags for each (requested and supported) must match exactly.
    // 2) The requested format must be unique, and a PCM format (we don't know
    //    how to test compatibility for compressed bitstream formats right now)
    // 3) The requested format must intersect the set of supported formats.
    //
    // Start by testing requirement #1.
    uint32_t requested_flags = sample_format & AUDIO_SAMPLE_FORMAT_FLAG_MASK;
    uint32_t supported_flags = format_range.sample_formats & AUDIO_SAMPLE_FORMAT_FLAG_MASK;
    if (requested_flags != supported_flags)
        return false;

    // Requirement #2.  If this format is unique and PCM, then there is exactly
    // 1 bit set in it and that bit is not AUDIO_SAMPLE_FORMAT_BITSTREAM.  We
    // can use fbl::is_pow2 to check if there is exactly 1 bit set.  (note,
    // fbl::is_pow2 does not consider 0 to be a power of 2, so it's perfect for
    // this)
    uint32_t requested_noflags = sample_format & ~AUDIO_SAMPLE_FORMAT_FLAG_MASK;
    if ((requested_noflags == AUDIO_SAMPLE_FORMAT_BITSTREAM) ||
        (!fbl::is_pow2(requested_noflags)))
        return false;

    // Requirement #3.  Testing intersection is easy, just and the two.  No need
    // to strip the flags from the supported format bitmask, we have already
    // stripped them from the request when checking requirement #2.
    if (!(format_range.sample_formats & requested_noflags))
        return false;

    // Check the requested frame rate.  If it is not in the range expressed by
    // the format_range, then we know this is not a match.
    if ((frame_rate < format_range.min_frames_per_second) ||
        (frame_rate > format_range.max_frames_per_second))
        return false;

    // The frame rate is in range, if this format_range supports continuous
    // frame rates, then this is a match.
    if (format_range.flags & ASF_RANGE_FLAG_FPS_CONTINUOUS)
        return true;

    // Check the 48k family.
    if ((format_range.flags & ASF_RANGE_FLAG_FPS_48000_FAMILY) && FrameRateIn48kFamily(frame_rate))
        return true;

    // Check the 44.1k family.
    if ((format_range.flags & ASF_RANGE_FLAG_FPS_44100_FAMILY) && FrameRateIn441kFamily(frame_rate))
        return true;

    // No supported frame rates found.  Declare no-match.
    return false;
}

FrameRateEnumerator::iterator::iterator(const FrameRateEnumerator* enumerator)
    : enumerator_(enumerator) {
    // If we have no enumerator, then we cannot advance to the first valid frame
    // rate.  Just get out.
    if (!enumerator_)
        return;

    // Sanity check our range first.  If it is continuous, or invalid in any
    // way, then we are not going to enumerate any valid frame rates.  Just set
    // our enumerator to nullptr and get out.
    const auto& range = enumerator_->range();
    if ((range.flags & ASF_RANGE_FLAG_FPS_CONTINUOUS) ||
       !(range.flags & DISCRETE_FLAGS) ||
        (range.min_frames_per_second > range.max_frames_per_second)) {
        enumerator_ = nullptr;
        return;
    }

    // Reset our current iterator state, then advance to the first valid
    // frame rate (if any)
    cur_flag_ = ASF_RANGE_FLAG_FPS_48000_FAMILY;
    fmt_ndx_  = static_cast<uint16_t>(-1);
    Advance();
}

void FrameRateEnumerator::iterator::Advance() {
    if (enumerator_ == nullptr) {
        MX_DEBUG_ASSERT(!cur_rate_ && !cur_flag_ && !fmt_ndx_);
        return;
    }

    const auto& range = enumerator_->range();

    while (cur_flag_ & DISCRETE_FLAGS) {
        const uint32_t* rates;
        uint16_t rates_count;

        if (cur_flag_ == ASF_RANGE_FLAG_FPS_48000_FAMILY) {
            rates = RATES_48000_FAMILY;
            rates_count = sizeof(RATES_48000_FAMILY);
        } else {
            MX_DEBUG_ASSERT(cur_flag_ == ASF_RANGE_FLAG_FPS_44100_FAMILY);
            rates = RATES_44100_FAMILY;
            rates_count = sizeof(RATES_44100_FAMILY);
        }

        if (range.flags & cur_flag_) {
            for (++fmt_ndx_; fmt_ndx_ < rates_count; ++fmt_ndx_) {
                uint32_t rate = rates[fmt_ndx_];

                // If the rate in the table is less than the minimum
                // frames_per_second, keep advancing the index.
                if (rate < range.min_frames_per_second)
                    continue;

                // If the rate in the table is greater than the maximum
                // frames_per_second, then we are done with this table.  There are
                // no more matches to be found in it.
                if (rate > range.max_frames_per_second)
                    break;

                // The rate in this table is between the min and the max rates
                // supported by this range.  Record it and get out.
                cur_rate_ = rate;
                return;
            }
        }

        // We are done with this table.  If we were searching the 48KHz family,
        // move on to the 44.1KHz family.  Otherwise, we are finished.
        if (cur_flag_ == ASF_RANGE_FLAG_FPS_48000_FAMILY) {
            cur_flag_ = ASF_RANGE_FLAG_FPS_44100_FAMILY;
            fmt_ndx_  = static_cast<uint16_t>(-1);
        } else {
            break;
        }
    }

    memset(this, 0, sizeof(*this));
}

}  // namespace utils
}  // namespace audio
