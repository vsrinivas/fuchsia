// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_wavl_tree.h>
#include <fbl/unique_ptr.h>

#include "drivers/audio/intel-hda/utils/codec-commands.h"

#include "codec_state.h"
#include "intel_hda_device.h"

namespace audio {
namespace intel_hda {

struct CodecVerb;
struct CodecResponse;

class IntelHDACodec : public IntelHDADevice,
                      public fbl::WAVLTreeContainable<fbl::unique_ptr<IntelHDACodec>> {
public:
    using CodecTree = fbl::WAVLTree<uint32_t, fbl::unique_ptr<IntelHDACodec>>;

    template <typename TARGET_TYPE>
    struct CommandListEntry {
        CodecVerb verb;
        mx_status_t (*parser)(TARGET_TYPE& target, const CodecResponse& resp);
    };

    mx_status_t DumpCodec(int argc, const char** argv);

    uint32_t id()     const { return codec_id_; }
    uint32_t GetKey() const { return id(); }

    static mx_status_t Enumerate();
    static CodecTree& codecs() { return codecs_; }

private:
    friend class fbl::unique_ptr<IntelHDACodec>;

    mx_status_t DoCodecCmd(uint16_t nid, const CodecVerb& verb, CodecResponse* resp_out);
    mx_status_t ReadCodecState();
    mx_status_t ReadFunctionGroupState(FunctionGroupStatePtr& ptr, uint16_t nid);
    mx_status_t ReadAudioFunctionGroupState(AudioFunctionGroupState& afg);
    mx_status_t ReadAudioWidgetState(AudioWidgetState& widget);
    mx_status_t ReadConnList(AudioWidgetState& widget);
    mx_status_t ReadAmpState(uint16_t nid, bool is_input, uint8_t ndx,
                             const AmpCaps& caps,
                             AudioWidgetState::AmpState* state_out);

    template <typename T>
    mx_status_t RunCommandList(T& target,
                               uint16_t nid,
                               const CommandListEntry<T>* cmds,
                               size_t cmd_count);

    IntelHDACodec(uint32_t codec_id, const char* const dev_name)
        : IntelHDADevice(dev_name),
          codec_id_(codec_id) { }

    ~IntelHDACodec() { }

    const uint32_t codec_id_;
    CodecState codec_state_;

    static CodecTree codecs_;
};

}  // namespace audio
}  // namespace intel_hda
