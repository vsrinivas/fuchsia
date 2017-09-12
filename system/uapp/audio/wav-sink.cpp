// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>
#include <fbl/auto_call.h>
#include <fbl/algorithm.h>
#include <fbl/limits.h>
#include <fdio/io.h>
#include <stdio.h>

#include "wav-sink.h"

zx_status_t WAVSink::SetFormat(const AudioStream::Format& format) {
    WAVHeader wav_hdr;

    if ((fd_ < 0) || format_set_) {
        return ZX_ERR_BAD_STATE;
    }

    if (format.channels > 8) return ZX_ERR_INVALID_ARGS;
    if (format.frame_rate == 0) return ZX_ERR_INVALID_ARGS;

    bool inv_endian = (format.sample_format & AUDIO_SAMPLE_FORMAT_FLAG_INVERT_ENDIAN) != 0;
    bool unsigned_fmt = (format.sample_format & AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED) != 0;
    auto noflag_format = static_cast<audio_sample_format_t>(
            (format.sample_format & ~AUDIO_SAMPLE_FORMAT_FLAG_MASK));

    // TODO(johngro): deal with endianness.  Right now, we just assume that we
    // are on a little endian system and demand that the samples given to us be
    // in host-endian (aka, little).
    if (inv_endian) return ZX_ERR_NOT_SUPPORTED;

    wav_hdr.wave_four_cc = WAVE_FOUR_CC;
    wav_hdr.fmt_four_cc = FMT_FOUR_CC;
    wav_hdr.fmt_chunk_len = sizeof(wav_hdr) - offsetof(WAVHeader, format);
    wav_hdr.channel_count = format.channels;
    wav_hdr.frame_rate = format.frame_rate;

    // TODO(johngro): Add support for some of these unsupported formats (signed
    // 8-bit, 20 or 24 bit in 32, etc...) by converting to the nearest WAV
    // compatible format on the fly.
    //
    // Only 8 bit formats are unsigned.
    if ((noflag_format == AUDIO_SAMPLE_FORMAT_8BIT) != unsigned_fmt)
        return ZX_ERR_NOT_SUPPORTED;

    wav_hdr.format = FORMAT_LPCM;

    switch (noflag_format) {
    // 8-bit WAV PCM is unsigned.
    case AUDIO_SAMPLE_FORMAT_8BIT:         wav_hdr.bits_per_sample = 8; break;
    case AUDIO_SAMPLE_FORMAT_16BIT:        wav_hdr.bits_per_sample = 16; break;
    case AUDIO_SAMPLE_FORMAT_24BIT_PACKED: wav_hdr.bits_per_sample = 24; break;

    case AUDIO_SAMPLE_FORMAT_32BIT_FLOAT:
        wav_hdr.format = FORMAT_IEEE_FLOAT;
        // deliberate fall-thru
    case AUDIO_SAMPLE_FORMAT_20BIT_IN32:
    case AUDIO_SAMPLE_FORMAT_24BIT_IN32:
    case AUDIO_SAMPLE_FORMAT_32BIT:        wav_hdr.bits_per_sample = 32; break;

    case AUDIO_SAMPLE_FORMAT_20BIT_PACKED:
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }

    wav_hdr.frame_size = static_cast<uint16_t>((wav_hdr.bits_per_sample >> 3u) *
                                                wav_hdr.channel_count);
    wav_hdr.average_byte_rate = static_cast<uint32_t>(wav_hdr.frame_size) * wav_hdr.frame_rate;

    zx_status_t res;
    RIFFChunkHeader riff_chunk;

    // Note: we don't know the length of our RIFF chunk or our DATA chunk yet;
    // we will come back and fill these out during finalize, but (for the time
    // being) we attempt to get as close as possible to correct.
    riff_chunk.four_cc = RIFF_FOUR_CC;
    riff_chunk.length  = sizeof(RIFFChunkHeader) + sizeof(WAVHeader);
    riff_chunk.FixupEndian();
    res = Write(&riff_chunk, sizeof(riff_chunk));
    if (res != ZX_OK) {
        printf("Failed to write top level RIFF header (res = %d)\n", res);
        return res;
    }

    wav_hdr.FixupEndian();
    res = Write(&wav_hdr, sizeof(wav_hdr));
    if (res != ZX_OK) {
        printf("Failed to write WAVE header (res = %d)\n", res);
        return res;
    }

    riff_chunk.four_cc = DATA_FOUR_CC;
    riff_chunk.length  = 0;
    riff_chunk.FixupEndian();
    res = Write(&riff_chunk, sizeof(riff_chunk));
    if (res != ZX_OK) {
        printf("Failed to write DATA header (res = %d)\n", res);
        return res;
    }

    format_set_ = true;
    return ZX_OK;
}

zx_status_t WAVSink::PutFrames(const void* buffer, uint32_t amt) {
    ZX_DEBUG_ASSERT(buffer != nullptr);

    if ((fd_ < 0) || !format_set_) {
        return ZX_ERR_BAD_STATE;
    }

    zx_status_t res = Write(buffer, amt);
    if (res != ZX_OK) {
        printf("Error writing %u bytes to WAV output (res %d)\n", amt, res);
        return res;
    }

    bytes_written_ += amt;
    return res;
}

zx_status_t WAVSink::Finalize() {
    if ((fd_ < 0) || !format_set_) {
        return ZX_ERR_BAD_STATE;
    }

    constexpr size_t riff_overhead = sizeof(RIFFChunkHeader) + sizeof(WAVHeader);
    auto riff_size = fbl::min<uint64_t>(fbl::numeric_limits<uint32_t>::max(),
                                         bytes_written_ + riff_overhead);
    auto data_size = fbl::min<uint64_t>(fbl::numeric_limits<uint32_t>::max(),
                                         bytes_written_);

    zx_status_t res;
    RIFFChunkHeader riff_chunk;

    res = Seek(0);
    if (res != 0) {
        printf("Failed to seek to RIFF header location during finalize (res = %d)\n", res);
        return res;
    }

    riff_chunk.four_cc = RIFF_FOUR_CC;
    riff_chunk.length  = static_cast<uint32_t>(riff_size);
    riff_chunk.FixupEndian();
    res = Write(&riff_chunk, sizeof(riff_chunk));
    if (res != 0) {
        printf("Failed finalize top level RIFF header (res = %d)\n", res);
        return res;
    }

    res = Seek(riff_overhead);
    if (res != 0) {
        printf("Failed to seek to DATA header location during finalize (res = %d)\n", res);
        return res;
    }

    riff_chunk.four_cc = DATA_FOUR_CC;
    riff_chunk.length  = static_cast<uint32_t>(data_size);
    riff_chunk.FixupEndian();
    res = Write(&riff_chunk, sizeof(riff_chunk));
    if (res != 0) {
        printf("Failed finalize DATA header (res = %d)\n", res);
        return res;
    }

    Close();
    format_set_ = false;
    bytes_written_ = 0;

    return ZX_OK;
}
