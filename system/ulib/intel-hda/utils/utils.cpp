// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls/object.h>

#include <intel-hda/utils/utils.h>

namespace audio {
namespace intel_hda {

static constexpr audio_sample_format_t AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT =
    static_cast<audio_sample_format_t>(AUDIO_SAMPLE_FORMAT_8BIT |
                                       AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED);

static constexpr audio_sample_format_t AUDIO_SAMPLE_FORMAT_NONE =
    static_cast<audio_sample_format_t>(0u);

struct FrameRateLut {
    uint32_t flag;
    uint32_t rate;
};

// Note: these LUTs must be kept in monotonically ascending order.
static const FrameRateLut FRAME_RATE_LUT_48K[] = {
    { IHDA_PCM_RATE_8000,   8000 },
    { IHDA_PCM_RATE_16000,  16000 },
    { IHDA_PCM_RATE_32000,  32000 },
    { IHDA_PCM_RATE_48000,  48000 },
    { IHDA_PCM_RATE_96000,  96000 },
    { IHDA_PCM_RATE_192000, 192000 },
    { IHDA_PCM_RATE_384000, 384000 },
};

static const FrameRateLut FRAME_RATE_LUT_44_1K[] = {
    { IHDA_PCM_RATE_11025,  11025 },
    { IHDA_PCM_RATE_22050,  22050 },
    { IHDA_PCM_RATE_44100,  44100 },
    { IHDA_PCM_RATE_88200,  88200 },
    { IHDA_PCM_RATE_176400, 176400 },
};

static const struct {
    const FrameRateLut* lut;
    size_t lut_size;
    uint16_t family_flag;
} FRAME_RATE_LUTS[] = {
    { FRAME_RATE_LUT_48K,   countof(FRAME_RATE_LUT_48K),   ASF_RANGE_FLAG_FPS_48000_FAMILY },
    { FRAME_RATE_LUT_44_1K, countof(FRAME_RATE_LUT_44_1K), ASF_RANGE_FLAG_FPS_44100_FAMILY },
};

zx_obj_type_t GetHandleType(const zx::handle& handle) {
    zx_info_handle_basic_t basic_info;

    if (!handle.is_valid())
        return ZX_OBJ_TYPE_NONE;

    zx_status_t res = handle.get_info(ZX_INFO_HANDLE_BASIC,
                                      &basic_info, sizeof(basic_info),
                                      nullptr, nullptr);

    return (res == ZX_OK) ? static_cast<zx_obj_type_t>(basic_info.type) : ZX_OBJ_TYPE_NONE;
}

zx_status_t MakeFormatRangeList(const SampleCaps& sample_caps,
                                uint32_t max_channels,
                                fbl::Vector<audio_stream_format_range_t>* ranges) {
    if (ranges == nullptr || ranges->size()) return ZX_ERR_INVALID_ARGS;
    if (!max_channels) return ZX_ERR_INVALID_ARGS;

    // Signed and unsigned formats require separate audio_sample_format_t
    // encodings.  8-bit is the only unsigned format supported by IHDA, however.
    // Compute the set signed formats that this stream supports, check for
    // unsigned 8 bit support in the process.
    auto signed_formats = AUDIO_SAMPLE_FORMAT_NONE;
    bool unsigned_8bit_supported = false;

    if (sample_caps.pcm_formats_ & IHDA_PCM_FORMAT_PCM) {
        static const struct {
            uint32_t ihda_flag;
            audio_sample_format_t audio_flag;
        } FORMAT_LUT[] = {
            { IHDA_PCM_SIZE_32BITS, AUDIO_SAMPLE_FORMAT_32BIT },
            { IHDA_PCM_SIZE_24BITS, AUDIO_SAMPLE_FORMAT_24BIT_IN32 },
            { IHDA_PCM_SIZE_20BITS, AUDIO_SAMPLE_FORMAT_20BIT_IN32 },
            { IHDA_PCM_SIZE_16BITS, AUDIO_SAMPLE_FORMAT_16BIT },
        };

        for (const auto& f : FORMAT_LUT) {
            if (sample_caps.pcm_size_rate_ & f.ihda_flag) {
                signed_formats =
                    static_cast<audio_sample_format_t>(signed_formats | f.audio_flag);
            }
        }

        if (sample_caps.pcm_size_rate_ & IHDA_PCM_SIZE_8BITS) {
            unsigned_8bit_supported = true;
        }
    }

    // If float is supported, add that into the set of signed formats that we
    // support.
    if (sample_caps.pcm_formats_ & IHDA_PCM_FORMAT_FLOAT32) {
        signed_formats =
            static_cast<audio_sample_format_t>(signed_formats | AUDIO_SAMPLE_FORMAT_32BIT_FLOAT);
    }

    // If we do not support any sample formats, simply get out early.  There is
    // no point in trying to compute the frame rate ranges.
    if (!signed_formats && !unsigned_8bit_supported)
        return ZX_OK;

    // Next, produce the sets of frame rates in the 48 and 44.1KHz frame rate
    // families which can be expressed using the [min, max] notation.  In
    // theory, it might be possible to combine each of these into a single
    // audio_stream_format_range_t entry, but to keep things simple, we don't
    // try to do so.
    for (const auto& family : FRAME_RATE_LUTS) {
        bool active_range = false;
        uint32_t min_rate, max_rate;

        for (size_t i = 0; i < family.lut_size; ++i) {
            const auto& entry = family.lut[i];
            bool supported_rate = (sample_caps.pcm_size_rate_ & entry.flag) != 0;

            // If this rate is supported, then either start a new range of
            // contiguous rates (if this is the first in this range) or bump up
            // the max rate if we are already building a range.
            if (supported_rate) {
                if (!active_range) {
                    min_rate = entry.rate;
                    max_rate = entry.rate;
                    active_range = true;
                } else {
                    max_rate = entry.rate;
                }
            }

            // If we have an active range of contiguous rates, and we have
            // either encountered a gap in the supported rates or this is the
            // last rate in the family, then produce the format ranges needed
            // for this range of rates.
            if (active_range && (!supported_rate || (i == family.lut_size))) {
                audio_stream_format_range_t range;

                range.min_frames_per_second = min_rate;
                range.max_frames_per_second = max_rate;
                range.min_channels = 1;
                range.max_channels = static_cast<uint8_t>(max_channels);
                range.flags = family.family_flag;

                if (signed_formats) {
                    range.sample_formats = signed_formats;

                    fbl::AllocChecker ac;
                    ranges->push_back(range, &ac);
                    if (!ac.check())
                        return ZX_ERR_NO_MEMORY;
                }

                if (unsigned_8bit_supported) {
                    range.sample_formats = AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT;

                    fbl::AllocChecker ac;
                    ranges->push_back(range, &ac);
                    if (!ac.check())
                        return ZX_ERR_NO_MEMORY;
                }

                active_range = false;
            }
        }
    }

    return ZX_OK;
}

}  // namespace intel_hda
}  // namespace audio
