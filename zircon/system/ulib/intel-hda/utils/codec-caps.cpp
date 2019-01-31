// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <intel-hda/utils/codec-caps.h>

namespace audio {
namespace intel_hda {

bool SampleCaps::SupportsRate(uint32_t rate) const {
    switch (rate) {
        case 384000: return ((pcm_size_rate_ & IHDA_PCM_RATE_384000) != 0);
        case 192000: return ((pcm_size_rate_ & IHDA_PCM_RATE_192000) != 0);
        case 176400: return ((pcm_size_rate_ & IHDA_PCM_RATE_176400) != 0);
        case 96000:  return ((pcm_size_rate_ & IHDA_PCM_RATE_96000) != 0);
        case 88200:  return ((pcm_size_rate_ & IHDA_PCM_RATE_88200) != 0);
        case 48000:  return ((pcm_size_rate_ & IHDA_PCM_RATE_48000) != 0);
        case 44100:  return ((pcm_size_rate_ & IHDA_PCM_RATE_44100) != 0);
        case 32000:  return ((pcm_size_rate_ & IHDA_PCM_RATE_32000) != 0);
        case 22050:  return ((pcm_size_rate_ & IHDA_PCM_RATE_22050) != 0);
        case 16000:  return ((pcm_size_rate_ & IHDA_PCM_RATE_16000) != 0);
        case 11025:  return ((pcm_size_rate_ & IHDA_PCM_RATE_11025) != 0);
        case 8000:   return ((pcm_size_rate_ & IHDA_PCM_RATE_8000) != 0);
    }

    return false;
}

bool SampleCaps::SupportsFormat(audio_sample_format_t sample_format) const {
    // Intel HDA controllers should always use host-endian for the samples fed
    // to the DMA engine, and do not support unsigned audio samples.
    if (sample_format & (AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED |
                         AUDIO_SAMPLE_FORMAT_FLAG_INVERT_ENDIAN))
        return false;

    // TODO(johngro) : Should we be doing specific bitstream/codec matching for
    // compressed audio passthru?  If so, how?
    if (sample_format == AUDIO_SAMPLE_FORMAT_BITSTREAM)
        return ((pcm_formats_ & IHDA_PCM_FORMAT_AC3) != 0);

    // If the user want's float32, need to have support for float (in the
    // formats field) and 32 bit containers (in the size/rate field)
    if (sample_format == AUDIO_SAMPLE_FORMAT_32BIT_FLOAT)
        return ((pcm_formats_ & IHDA_PCM_FORMAT_FLOAT32) != 0)
            && ((pcm_size_rate_ & AUDIO_SAMPLE_FORMAT_32BIT) != 0);

    // User is requesting a PCM format.  Start with checking for basic support,
    // then match the user's request against the set of containers Intel HDA can
    // support.
    if (!(pcm_formats_ & IHDA_PCM_FORMAT_PCM))
        return false;

    switch (sample_format) {
        case AUDIO_SAMPLE_FORMAT_8BIT:       return ((pcm_size_rate_ & IHDA_PCM_SIZE_8BITS) != 0);
        case AUDIO_SAMPLE_FORMAT_16BIT:      return ((pcm_size_rate_ & IHDA_PCM_SIZE_16BITS) != 0);
        case AUDIO_SAMPLE_FORMAT_20BIT_IN32: return ((pcm_size_rate_ & IHDA_PCM_SIZE_20BITS) != 0);
        case AUDIO_SAMPLE_FORMAT_24BIT_IN32: return ((pcm_size_rate_ & IHDA_PCM_SIZE_24BITS) != 0);
        case AUDIO_SAMPLE_FORMAT_32BIT:      return ((pcm_size_rate_ & IHDA_PCM_SIZE_32BITS) != 0);
        default: return false;
    }
}

}  // namespace intel_hda
}  // namespace audio
