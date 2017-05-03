// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "realtek-stream.h"

namespace audio {
namespace intel_hda {
namespace codecs {

mx_status_t RealtekStream::DisableConverterLocked(bool force_all) {
    const CommandListEntry DISABLE_CONVERTER_VERBS[] = {
        { props_.conv_nid, SET_AMPLIFIER_GAIN_MUTE(true, 0, is_input(), !is_input()) },
        { props_.pc_nid,   SET_AMPLIFIER_GAIN_MUTE(true, 0, is_input(), !is_input()) },
        { props_.conv_nid, SET_CONVERTER_STREAM_CHAN(IHDA_INVALID_STREAM_TAG, 0) },
        { props_.conv_nid, SET_POWER_STATE(HDA_PS_D3HOT) },
        { props_.pc_nid,   SET_POWER_STATE(HDA_PS_D3HOT) },
    };

    return RunCmdListLocked(DISABLE_CONVERTER_VERBS, countof(DISABLE_CONVERTER_VERBS), force_all);
}

mx_status_t RealtekStream::RunCmdListLocked(const CommandListEntry* list,
                                           size_t count,
                                           bool force_all) {
    MX_DEBUG_ASSERT(list);

    mx_status_t total_res = NO_ERROR;
    for (size_t i = 0; i < count; ++i) {
        const auto& entry = list[i];

        mx_status_t res = SendCodecCommandLocked(entry.nid, entry.verb, Ack::NO);
        if ((res != NO_ERROR) && !force_all)
            return res;

        if (total_res == NO_ERROR)
            total_res = res;
    }

    return total_res;
}

mx_status_t RealtekStream::OnActivateLocked() {
    return DisableConverterLocked();
}

void RealtekStream::OnDeactivateLocked() {
    DisableConverterLocked(true);
}

mx_status_t RealtekStream::BeginChangeStreamFormatLocked(const audio2_proto::StreamSetFmtReq& fmt) {
    // TODO(johngro) : Query the formats supported by the converter instead of
    // hardcoding them.

    // Check the format arguments.
    if ((fmt.channels != 1) && (fmt.channels != 2))
        return ERR_NOT_SUPPORTED;

    if ((fmt.sample_format != AUDIO2_SAMPLE_FORMAT_16BIT) &&
        (fmt.sample_format != AUDIO2_SAMPLE_FORMAT_20BIT_IN32) &&
        (fmt.sample_format != AUDIO2_SAMPLE_FORMAT_24BIT_IN32))
        return ERR_NOT_SUPPORTED;

    switch (fmt.frames_per_second) {
    case 48000:
    case 44100:
        break;
    default:
        return ERR_NOT_SUPPORTED;
    }

    // Looks good, make sure that the converter is muted and not processing any stream tags.
    return DisableConverterLocked();
}

mx_status_t RealtekStream::FinishChangeStreamFormatLocked(uint16_t encoded_fmt) {
    const CommandListEntry ENABLE_CONVERTER_VERBS[] = {
        { props_.conv_nid, SET_CONVERTER_FORMAT(encoded_fmt) },
        { props_.conv_nid, SET_CONVERTER_STREAM_CHAN(dma_stream_tag(), 0) },
        { props_.pc_nid,   SET_POWER_STATE(HDA_PS_D0) },
        { props_.conv_nid, SET_POWER_STATE(HDA_PS_D0) },
        { props_.pc_nid,   SET_ANALOG_PIN_WIDGET_CTRL(!is_input(),
                                                       is_input(),
                                                       props_.headphone_out) },
        { props_.conv_nid, SET_AMPLIFIER_GAIN_MUTE(false,
                                                   props_.conv_unity_gain_lvl,
                                                   is_input(),
                                                   !is_input()) },
        { props_.pc_nid,   SET_AMPLIFIER_GAIN_MUTE(false,
                                                   props_.pc_unity_gain_lvl,
                                                   is_input(),
                                                   !is_input()) },
    };

    return RunCmdListLocked(ENABLE_CONVERTER_VERBS, countof(ENABLE_CONVERTER_VERBS));
}

}  // namespace codecs
}  // namespace audio
}  // namespace intel_hda
