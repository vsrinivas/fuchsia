// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "qemu-stream.h"

#include <fbl/vector.h>

namespace audio {
namespace intel_hda {
namespace codecs {

static constexpr uint8_t UNITY_GAIN = 74;
static const audio_stream_unique_id_t microphone_id = AUDIO_STREAM_UNIQUE_ID_BUILTIN_MICROPHONE;
static const audio_stream_unique_id_t speaker_id = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;

QemuStream::QemuStream(uint32_t stream_id, bool is_input, uint16_t converter_nid)
    : IntelHDAStreamBase(stream_id, is_input),
      converter_nid_(converter_nid) {
    SetPersistentUniqueId(is_input ? microphone_id : speaker_id);
}

zx_status_t QemuStream::DisableConverterLocked(bool force_all) {
    const CodecVerb DISABLE_CONVERTER_VERBS[] = {
        SET_AMPLIFIER_GAIN_MUTE(true, 0, is_input(), !is_input()),
        SET_CONVERTER_STREAM_CHAN(IHDA_INVALID_STREAM_TAG, 0),
    };

    return RunCmdListLocked(DISABLE_CONVERTER_VERBS, countof(DISABLE_CONVERTER_VERBS), force_all);
}

zx_status_t QemuStream::RunCmdListLocked(const CodecVerb* list, size_t count, bool force_all) {
    ZX_DEBUG_ASSERT(list);

    zx_status_t total_res = ZX_OK;
    for (size_t i = 0; i < count; ++i) {
        const auto& verb = list[i];

        zx_status_t res = SendCodecCommandLocked(converter_nid_, verb, Ack::NO);
        if ((res != ZX_OK) && !force_all)
            return res;

        if (total_res == ZX_OK)
            total_res = res;
    }

    return total_res;
}

zx_status_t QemuStream::OnActivateLocked() {
    fbl::Vector<audio_proto::FormatRange> supported_formats;

    audio_proto::FormatRange range;
    range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
    range.min_channels = 1;
    range.max_channels = 2;
    range.min_frames_per_second = 16000;
    range.max_frames_per_second = 96000;
    range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY | ASF_RANGE_FLAG_FPS_44100_FAMILY;

    fbl::AllocChecker ac;
    supported_formats.push_back(range, &ac);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    SetSupportedFormatsLocked(fbl::move(supported_formats));

    return DisableConverterLocked();
}

void QemuStream::OnDeactivateLocked() {
    DisableConverterLocked(true);
}

zx_status_t QemuStream::BeginChangeStreamFormatLocked(const audio_proto::StreamSetFmtReq& fmt) {
    return DisableConverterLocked();
}

zx_status_t QemuStream::FinishChangeStreamFormatLocked(uint16_t encoded_fmt) {
    const CodecVerb ENABLE_CONVERTER_VERBS[] = {
        SET_CONVERTER_FORMAT(encoded_fmt),
        SET_CONVERTER_STREAM_CHAN(dma_stream_tag(), 0),
        SET_AMPLIFIER_GAIN_MUTE(false, UNITY_GAIN, is_input(), !is_input()),
    };

    return RunCmdListLocked(ENABLE_CONVERTER_VERBS, countof(ENABLE_CONVERTER_VERBS));
}

void QemuStream::OnGetStringLocked(const audio_proto::GetStringReq& req,
                                   audio_proto::GetStringResp* out_resp) {
    ZX_DEBUG_ASSERT(out_resp);
    const char* str = nullptr;

    switch (req.id) {
        case AUDIO_STREAM_STR_ID_MANUFACTURER:
            str = "QEMU";
            break;

        case AUDIO_STREAM_STR_ID_PRODUCT:
            str = is_input() ? "Builtin Microphone" : "Builtin Speakers";
            break;

        default:
            IntelHDAStreamBase::OnGetStringLocked(req, out_resp);
            return;
    }

    int res = snprintf(reinterpret_cast<char*>(out_resp->str), sizeof(out_resp->str), "%s",
                       str ? str : "<unassigned>");
    ZX_DEBUG_ASSERT(res >= 0);
    out_resp->result = ZX_OK;
    out_resp->strlen = fbl::min<uint32_t>(res, sizeof(out_resp->str) - 1);
    out_resp->id = req.id;
}


}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio
