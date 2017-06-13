// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "qemu-stream.h"

namespace audio {
namespace intel_hda {
namespace codecs {

static constexpr uint8_t UNITY_GAIN = 74;

mx_status_t QemuStream::DisableConverterLocked(bool force_all) {
    const CodecVerb DISABLE_CONVERTER_VERBS[] = {
        SET_AMPLIFIER_GAIN_MUTE(true, 0, is_input(), !is_input()),
        SET_CONVERTER_STREAM_CHAN(IHDA_INVALID_STREAM_TAG, 0),
    };

    return RunCmdListLocked(DISABLE_CONVERTER_VERBS, countof(DISABLE_CONVERTER_VERBS), force_all);
}

mx_status_t QemuStream::RunCmdListLocked(const CodecVerb* list, size_t count, bool force_all) {
    MX_DEBUG_ASSERT(list);

    mx_status_t total_res = MX_OK;
    for (size_t i = 0; i < count; ++i) {
        const auto& verb = list[i];

        mx_status_t res = SendCodecCommandLocked(converter_nid_, verb, Ack::NO);
        if ((res != MX_OK) && !force_all)
            return res;

        if (total_res == MX_OK)
            total_res = res;
    }

    return total_res;
}

mx_status_t QemuStream::OnActivateLocked() {
    return DisableConverterLocked();
}

void QemuStream::OnDeactivateLocked() {
    DisableConverterLocked(true);
}

mx_status_t QemuStream::BeginChangeStreamFormatLocked(const audio2_proto::StreamSetFmtReq& fmt) {
    // Check the format arguments.
    if ((fmt.channels != 1) && (fmt.channels != 2))
        return MX_ERR_NOT_SUPPORTED;

    if (fmt.sample_format != AUDIO2_SAMPLE_FORMAT_16BIT)
        return MX_ERR_NOT_SUPPORTED;

    switch (fmt.frames_per_second) {
    case 96000:
    case 88200:
    case 48000:
    case 44100:
    case 32000:
    case 22050:
    case 16000:
        break;
    default:
        return MX_ERR_NOT_SUPPORTED;
    }

    // Looks good, make sure that the converter is muted and not processing any stream tags.
    return DisableConverterLocked();
}

mx_status_t QemuStream::FinishChangeStreamFormatLocked(uint16_t encoded_fmt) {
    const CodecVerb ENABLE_CONVERTER_VERBS[] = {
        SET_CONVERTER_FORMAT(encoded_fmt),
        SET_CONVERTER_STREAM_CHAN(dma_stream_tag(), 0),
        SET_AMPLIFIER_GAIN_MUTE(false, UNITY_GAIN, is_input(), !is_input()),
    };

    return RunCmdListLocked(ENABLE_CONVERTER_VERBS, countof(ENABLE_CONVERTER_VERBS));
}

}  // namespace codecs
}  // namespace audio
}  // namespace intel_hda
