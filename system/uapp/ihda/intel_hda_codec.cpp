// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel_hda_codec.h"

namespace audio {
namespace intel_hda {

IntelHDACodec::CodecTree IntelHDACodec::codecs_;

extern void print_codec_state(const CodecState& codec);

////////////////////////////////////////////////////////////////////////////////
//
// Parser and CommandList for fetching the currently configured unsolicited
// response state (present in both function groups and widgets)
//
////////////////////////////////////////////////////////////////////////////////
static zx_status_t ParseUnsolicitedResponseState(UnsolicitedResponseState& state,
                                                 const CodecResponse& resp) {
    // Section 7.3.3.14.
    state.raw_data_ = static_cast<uint8_t>(resp.data & 0xFF);
    return ZX_OK;
}

static const IntelHDACodec::CommandListEntry<UnsolicitedResponseState>
    FETCH_UNSOLICITED_RESPONSE_STATE[] = {
    { GET_UNSOLICITED_RESP_CTRL, ParseUnsolicitedResponseState },
};

////////////////////////////////////////////////////////////////////////////////
//
// Parsers and CommandLists for fetching info about supported and current power
// state.
//
////////////////////////////////////////////////////////////////////////////////
static zx_status_t ParseSupportedPowerStates(PowerState& ps, const CodecResponse& resp) {
    ps.supported_states_ = resp.data;
    return ZX_OK;
}

static zx_status_t ParseCurrentPowerState(PowerState& ps, const CodecResponse& resp) {
    // Section 7.3.3.10
    ps.set_            = static_cast<uint8_t>(resp.data & 0xF);
    ps.active_         = static_cast<uint8_t>((resp.data >> 4) & 0xF);
    ps.error_          = (resp.data & (1u <<  8)) != 0;
    ps.clock_stop_ok_  = (resp.data & (1u <<  9)) != 0;
    ps.settings_reset_ = (resp.data & (1u << 10)) != 0;

    return ZX_OK;
}

static const IntelHDACodec::CommandListEntry<PowerState> FETCH_POWER_STATE[] = {
    { GET_PARAM(CodecParam::SUPPORTED_PWR_STATES), ParseSupportedPowerStates },
    { GET_POWER_STATE,                             ParseCurrentPowerState },
};

////////////////////////////////////////////////////////////////////////////////
//
// Parsers and CommandLists for fetching info about audio widgets
//
////////////////////////////////////////////////////////////////////////////////
static zx_status_t ParseAWPcmSizeRate(AudioWidgetState& widget, const CodecResponse& resp) {
    auto& afg = *widget.afg_;
    const auto& caps = widget.caps_;

    widget.pcm_size_rate_ = caps.format_override()
                          ? resp.data
                          : afg.default_pcm_size_rate_;

    return ZX_OK;
}

static zx_status_t ParseAWPcmFormats(AudioWidgetState& widget, const CodecResponse& resp) {
    auto& afg = *widget.afg_;
    const auto& caps = widget.caps_;

    widget.pcm_formats_ = caps.format_override()
                        ? resp.data
                        : afg.default_pcm_formats_;

    return ZX_OK;
}

static zx_status_t ParseAWInputAmpCaps(AudioWidgetState& widget, const CodecResponse& resp) {
    auto& afg = *widget.afg_;
    const auto& caps = widget.caps_;

    if (caps.input_amp_present()) {
        widget.input_amp_caps_ = caps.amp_param_override()
                               ? AmpCaps(resp.data)
                               : afg.default_input_amp_caps_;
    }

    return ZX_OK;
}

static zx_status_t ParseAWOutputAmpCaps(AudioWidgetState& widget, const CodecResponse& resp) {
    auto& afg = *widget.afg_;
    const auto& caps = widget.caps_;

    if (caps.output_amp_present()) {
        widget.output_amp_caps_ = caps.amp_param_override()
                                ? AmpCaps(resp.data)
                                : afg.default_output_amp_caps_;
    }

    return ZX_OK;
}

static zx_status_t ParseAWConnectionListLen(AudioWidgetState& widget, const CodecResponse& resp) {
    const auto& caps = widget.caps_;

    if (caps.has_conn_list()) {
        widget.long_form_conn_list_ = ((resp.data & 0x80) != 0);
        widget.conn_list_len_ = resp.data & 0x7f;

        if (widget.conn_list_len_) {
            fbl::AllocChecker ac;
            widget.conn_list_.reset(
                    new (&ac) AudioWidgetState::ConnListEntry[widget.conn_list_len_]);
            if (!ac.check()) {
                return ZX_ERR_NO_MEMORY;
            }
        }
    } else {
        widget.long_form_conn_list_ = false;
        widget.conn_list_len_ = 0;
    }

    return ZX_OK;
}

static zx_status_t ParseAWProcessingCaps(AudioWidgetState& widget, const CodecResponse& resp) {
    const auto& caps = widget.caps_;

    if (caps.proc_widget()) {
        widget.can_bypass_processing_ = (resp.data & 0x1) != 0;
        widget.processing_coefficient_count_ = ((resp.data >> 8) & 0xFF);
    }

    return ZX_OK;
}

static zx_status_t ParseAWPinCaps(AudioWidgetState& widget, const CodecResponse& resp) {
    widget.pin_caps_ = resp.data;
    return ZX_OK;
}

static zx_status_t ParseAWVolumeKnobCaps(AudioWidgetState& widget, const CodecResponse& resp) {
    widget.vol_knob_is_delta_ = (resp.data & 0x80) != 0;
    widget.vol_knob_steps_    = (resp.data & 0x7f);
    return ZX_OK;
}

static zx_status_t ParseAWStreamChan(AudioWidgetState& widget, const CodecResponse& resp) {
    // Section 7.3.3.11 and Table 85
    widget.stream_tag_  = static_cast<uint8_t>((resp.data >> 4) & 0xF);
    widget.stream_chan_ = static_cast<uint8_t>(resp.data & 0xF);
    return ZX_OK;
}

static zx_status_t ParseAWConfigDefaults(AudioWidgetState& widget, const CodecResponse& resp) {
    widget.cfg_defaults_.raw_data_ = resp.data;
    return ZX_OK;
}

static zx_status_t ParseAWPinWidgetCtrl(AudioWidgetState& widget, const CodecResponse& resp) {
    widget.pin_widget_ctrl_.raw_data_ = static_cast<uint8_t>(resp.data & 0xFF);
    return ZX_OK;
}

static zx_status_t ParseAudioWidgetType(AudioWidgetStatePtr& ptr, const CodecResponse& resp) {
    AudioWidgetCaps caps(resp.data);

    switch (caps.type()) {
    case AudioWidgetCaps::Type::OUTPUT:
    case AudioWidgetCaps::Type::INPUT:
    case AudioWidgetCaps::Type::MIXER:
    case AudioWidgetCaps::Type::SELECTOR:
    case AudioWidgetCaps::Type::PIN_COMPLEX:
    case AudioWidgetCaps::Type::POWER:
    case AudioWidgetCaps::Type::VOLUME_KNOB:
    case AudioWidgetCaps::Type::BEEP_GEN:
    case AudioWidgetCaps::Type::VENDOR:
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AllocChecker ac;
    ptr.reset(new (&ac) AudioWidgetState(caps));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    return ZX_OK;
}

static const IntelHDACodec::CommandListEntry<AudioWidgetState> FETCH_AUDIO_INPUT_CAPS[] = {
    { GET_PARAM(CodecParam::SUPPORTED_PCM_SIZE_RATE),  ParseAWPcmSizeRate },
    { GET_PARAM(CodecParam::SUPPORTED_STREAM_FORMATS), ParseAWPcmFormats },
    { GET_PARAM(CodecParam::INPUT_AMP_CAPS),           ParseAWInputAmpCaps },
    { GET_PARAM(CodecParam::CONNECTION_LIST_LEN),      ParseAWConnectionListLen },
    { GET_PARAM(CodecParam::PROCESSING_CAPS),          ParseAWProcessingCaps },
    { GET_CONVERTER_STREAM_CHAN,                       ParseAWStreamChan },
};

static const IntelHDACodec::CommandListEntry<AudioWidgetState> FETCH_AUDIO_OUTPUT_CAPS[] = {
    { GET_PARAM(CodecParam::SUPPORTED_PCM_SIZE_RATE),  ParseAWPcmSizeRate },
    { GET_PARAM(CodecParam::SUPPORTED_STREAM_FORMATS), ParseAWPcmFormats },
    { GET_PARAM(CodecParam::OUTPUT_AMP_CAPS),          ParseAWOutputAmpCaps },
    { GET_PARAM(CodecParam::PROCESSING_CAPS),          ParseAWProcessingCaps },
    { GET_CONVERTER_STREAM_CHAN,                       ParseAWStreamChan },
};

static const IntelHDACodec::CommandListEntry<AudioWidgetState> FETCH_DIGITAL_PIN_COMPLEX_CAPS[] = {
    { GET_PARAM(CodecParam::PIN_CAPS),             ParseAWPinCaps },
    { GET_PARAM(CodecParam::OUTPUT_AMP_CAPS),      ParseAWOutputAmpCaps },
    { GET_PARAM(CodecParam::CONNECTION_LIST_LEN),  ParseAWConnectionListLen },
    { GET_PARAM(CodecParam::PROCESSING_CAPS),      ParseAWProcessingCaps },
    { GET_CONFIG_DEFAULT,                          ParseAWConfigDefaults },
    { GET_PIN_WIDGET_CTRL,                         ParseAWPinWidgetCtrl },
};

static const IntelHDACodec::CommandListEntry<AudioWidgetState>
FETCH_NON_DIGITAL_PIN_COMPLEX_CAPS[] = {
    { GET_PARAM(CodecParam::PIN_CAPS),             ParseAWPinCaps },
    { GET_PARAM(CodecParam::INPUT_AMP_CAPS),       ParseAWInputAmpCaps },
    { GET_PARAM(CodecParam::OUTPUT_AMP_CAPS),      ParseAWOutputAmpCaps },
    { GET_PARAM(CodecParam::CONNECTION_LIST_LEN),  ParseAWConnectionListLen },
    { GET_PARAM(CodecParam::PROCESSING_CAPS),      ParseAWProcessingCaps },
    { GET_CONFIG_DEFAULT,                          ParseAWConfigDefaults },
    { GET_PIN_WIDGET_CTRL,                         ParseAWPinWidgetCtrl },
};

static const IntelHDACodec::CommandListEntry<AudioWidgetState> FETCH_MIXER_CAPS[] = {
    { GET_PARAM(CodecParam::INPUT_AMP_CAPS),       ParseAWInputAmpCaps },
    { GET_PARAM(CodecParam::OUTPUT_AMP_CAPS),      ParseAWOutputAmpCaps },
    { GET_PARAM(CodecParam::CONNECTION_LIST_LEN),  ParseAWConnectionListLen },
};

static const IntelHDACodec::CommandListEntry<AudioWidgetState> FETCH_SELECTOR_CAPS[] = {
    { GET_PARAM(CodecParam::INPUT_AMP_CAPS),       ParseAWInputAmpCaps },
    { GET_PARAM(CodecParam::OUTPUT_AMP_CAPS),      ParseAWOutputAmpCaps },
    { GET_PARAM(CodecParam::CONNECTION_LIST_LEN),  ParseAWConnectionListLen },
    { GET_PARAM(CodecParam::PROCESSING_CAPS),      ParseAWProcessingCaps },
};

static const IntelHDACodec::CommandListEntry<AudioWidgetState> FETCH_POWER_CAPS[] = {
    { GET_PARAM(CodecParam::CONNECTION_LIST_LEN),  ParseAWConnectionListLen },
};

static const IntelHDACodec::CommandListEntry<AudioWidgetState> FETCH_VOLUME_KNOB_CAPS[] = {
    { GET_PARAM(CodecParam::CONNECTION_LIST_LEN),  ParseAWConnectionListLen },
    { GET_PARAM(CodecParam::VOLUME_KNOB_CAPS),     ParseAWVolumeKnobCaps },
};

static const IntelHDACodec::CommandListEntry<AudioWidgetStatePtr> FETCH_WIDGET_TYPE[] = {
    { GET_PARAM(CodecParam::AW_CAPS), ParseAudioWidgetType },
};

////////////////////////////////////////////////////////////////////////////////
//
// Parsers and CommandLists for fetching info about function groups.
//
////////////////////////////////////////////////////////////////////////////////
static zx_status_t ParseAFGCaps(AudioFunctionGroupState& afg, const CodecResponse& resp) {
    afg.caps_.raw_data_ = resp.data;
    return ZX_OK;
}

static zx_status_t ParseAFGPcmSizeRate(AudioFunctionGroupState& afg, const CodecResponse& resp) {
    // Section 7.3.4.7 : Supported PCM sizes and rates
    afg.default_pcm_size_rate_ = resp.data;
    return ZX_OK;
}

static zx_status_t ParseAFGPcmFormats(AudioFunctionGroupState& afg, const CodecResponse& resp) {
    afg.default_pcm_formats_ = resp.data;
    return ZX_OK;
}

static zx_status_t ParseAFGInputAmpCaps(AudioFunctionGroupState& afg, const CodecResponse& resp) {
    afg.default_input_amp_caps_.raw_data_ = resp.data;
    return ZX_OK;
}

static zx_status_t ParseAFGOutputAmpCaps(AudioFunctionGroupState& afg, const CodecResponse& resp) {
    afg.default_output_amp_caps_.raw_data_ = resp.data;
    return ZX_OK;
}

static zx_status_t ParseAFGGPIOCount(AudioFunctionGroupState& afg, const CodecResponse& resp) {
    // Section 7.3.4.14 : GPIO Counts
    afg.gpio_can_wake_             = (resp.data & 0x80000000) != 0;
    afg.gpio_can_send_unsolicited_ = (resp.data & 0x40000000) != 0;
    afg.gpi_count_                 = (resp.data >> 16) & 0xFF;
    afg.gpo_count_                 = (resp.data >>  8) & 0xFF;
    afg.gpio_count_                = (resp.data >>  0) & 0xFF;

    return ZX_OK;
}

static zx_status_t ParseAFGImplId(AudioFunctionGroupState& afg, const CodecResponse& resp) {
    afg.impl_id_.raw_data_ = resp.data;
    return ZX_OK;
}

static zx_status_t ParseAFGWidgetCount(AudioFunctionGroupState& afg, const CodecResponse& resp) {
    /* Response format documented in section 7.3.4.1 */
    afg.widget_count_       =  resp.data & 0xFF;
    afg.widget_starting_id_ = (resp.data >> 16) & 0xFF;
    uint32_t last_widget_nid  = static_cast<uint32_t>(afg.widget_starting_id_)
                              + afg.widget_count_ - 1;

    if (last_widget_nid > HDA_MAX_NID)
        return ZX_ERR_INTERNAL;

    if (afg.widget_count_) {
        fbl::AllocChecker ac;
        afg.widgets_.reset(new (&ac) AudioWidgetStatePtr[afg.widget_count_]);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
    }

    return ZX_OK;
}

static const IntelHDACodec::CommandListEntry<AudioFunctionGroupState> FETCH_AFG_PROPERTIES[] {
    { GET_PARAM(CodecParam::AFG_CAPS),                 ParseAFGCaps },
    { GET_PARAM(CodecParam::SUPPORTED_PCM_SIZE_RATE),  ParseAFGPcmSizeRate },
    { GET_PARAM(CodecParam::SUPPORTED_STREAM_FORMATS), ParseAFGPcmFormats },
    { GET_PARAM(CodecParam::INPUT_AMP_CAPS),           ParseAFGInputAmpCaps },
    { GET_PARAM(CodecParam::OUTPUT_AMP_CAPS),          ParseAFGOutputAmpCaps },
    { GET_PARAM(CodecParam::GPIO_COUNT),               ParseAFGGPIOCount },
    { GET_IMPLEMENTATION_ID,                           ParseAFGImplId },
    { GET_PARAM(CodecParam::SUBORDINATE_NODE_COUNT),   ParseAFGWidgetCount },
};

static zx_status_t ParseFnGroupType(FunctionGroupStatePtr& ptr, const CodecResponse& resp) {
    /* Response format documented in section 7.3.4.1 */
    auto type = static_cast<FunctionGroupState::Type>(resp.data & 0xFF);
    fbl::AllocChecker ac;

    switch (type) {
    case FunctionGroupState::Type::AUDIO:
        ptr.reset(new (&ac) AudioFunctionGroupState());
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        break;

    case FunctionGroupState::Type::MODEM:
        ptr.reset(new (&ac) ModemFunctionGroupState());
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        break;
    default:
        if ((type >= FunctionGroupState::Type::VENDOR_START) &&
            (type <= FunctionGroupState::Type::VENDOR_END)) {
            ptr.reset(new (&ac) VendorFunctionGroupState(type));
            if (!ac.check()) {
                return ZX_ERR_NO_MEMORY;
            }
        } else {
            return ZX_ERR_INTERNAL;
        }
        break;
    }

    ptr->can_send_unsolicited_ = ((resp.data & 0x100) != 0);
    return ZX_OK;
}

static const IntelHDACodec::CommandListEntry<FunctionGroupStatePtr> FETCH_FUNCTION_GROUP_TYPE[] = {
    { GET_PARAM(CodecParam::FUNCTION_GROUP_TYPE), ParseFnGroupType },
};

////////////////////////////////////////////////////////////////////////////////
//
// Parsers and command list for fetching info about core codec capabilities.
//
////////////////////////////////////////////////////////////////////////////////
static zx_status_t ParseVendorID(CodecState& codec, const CodecResponse& resp) {
    /* Response format documented in section 7.3.4.1 */

    codec.vendor_id_ = static_cast<uint16_t>((resp.data >> 16) & 0xFFFF);
    codec.device_id_ = static_cast<uint16_t>(resp.data & 0xFFFF);;

    return (codec.vendor_id_ != 0) ? ZX_OK : ZX_ERR_INTERNAL;
}

static zx_status_t ParseRevisionID(CodecState& codec, const CodecResponse& resp) {
    /* Response format documented in section 7.3.4.2 */

    codec.major_rev_          = (resp.data >> 20) & 0xF;
    codec.minor_rev_          = (resp.data >> 16) & 0xF;
    codec.vendor_rev_id_      = (resp.data >>  8) & 0xFF;
    codec.vendor_stepping_id_ = resp.data & 0xFF;

    return ZX_OK;
}

static zx_status_t ParseFnGroupCount(CodecState& codec, const CodecResponse& resp) {
    /* Response format documented in section 7.3.4.3 */

    codec.fn_group_count_ = resp.data & 0xFF;
    codec.fn_group_starting_id_ = (resp.data >> 16) & 0xFF;

    uint32_t last_fn_group_nid = static_cast<uint32_t>(codec.fn_group_starting_id_)
                               + codec.fn_group_count_ - 1;
    if (last_fn_group_nid > HDA_MAX_NID)
        return ZX_ERR_INTERNAL;

    // Allocate the storage for the function group state pointers, then
    // start the process of enumerating their properties and widgets.
    fbl::AllocChecker ac;
    codec.fn_groups_.reset(new (&ac) FunctionGroupStatePtr[codec.fn_group_count_]);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    return ZX_OK;
}

static const IntelHDACodec::CommandListEntry<CodecState> FETCH_CODEC_ROOT_COMMANDS[] = {
    { GET_PARAM(CodecParam::VENDOR_ID),              ParseVendorID },
    { GET_PARAM(CodecParam::REVISION_ID),            ParseRevisionID },
    { GET_PARAM(CodecParam::SUBORDINATE_NODE_COUNT), ParseFnGroupCount },
};

zx_status_t IntelHDACodec::Enumerate() {
    static const char* const DEV_PATH = "/dev/class/intel-hda-codec";

    zx_status_t res = ZirconDevice::Enumerate(nullptr, DEV_PATH,
    [](void*, uint32_t id, const char* const dev_name) -> zx_status_t {
        fbl::AllocChecker ac;
        auto codec = fbl::unique_ptr<IntelHDACodec>(new (&ac) IntelHDACodec(id, dev_name));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        if (codec == nullptr)
            return ZX_ERR_NO_MEMORY;

        if (!codecs_.insert_or_find(fbl::move(codec)))
            return ZX_ERR_INTERNAL;

        return ZX_OK;
    });

    return res;
}

zx_status_t IntelHDACodec::DumpCodec(int argc, const char** argv) {
    zx_status_t res = ReadCodecState();
    if (res != ZX_OK)
        return res;

    printf("Codec ID %u :: %s\n", codec_id_, dev_name_);
    print_codec_state(codec_state_);

    return ZX_OK;
}

#define RUN_COMMAND_LIST(_tgt, _nid, _list, _fail_msg, ...) {   \
    res = RunCommandList(_tgt, _nid, _list, countof(_list));    \
    if (res != ZX_OK) {                                      \
        printf(_fail_msg " (res %d)\n", ##__VA_ARGS__, res);    \
        return res;                                             \
    }                                                           \
}

zx_status_t IntelHDACodec::ReadCodecState() {
    zx_status_t res = Connect();

    if (res != ZX_OK)
        return res;

    codec_state_.reset();

    RUN_COMMAND_LIST(codec_state_, 0u, FETCH_CODEC_ROOT_COMMANDS,
                    "Failed while fetching codec root info");

    for (uint16_t group_ndx = 0; group_ndx < codec_state_.fn_group_count_; ++group_ndx) {
        auto& fn_group_ptr = codec_state_.fn_groups_[group_ndx];
        auto  nid          = static_cast<uint16_t>(group_ndx + codec_state_.fn_group_starting_id_);

        res = ReadFunctionGroupState(fn_group_ptr, nid);
        if (res != ZX_OK)
            return res;
    }

    return ZX_OK;
}

zx_status_t IntelHDACodec::ReadFunctionGroupState(FunctionGroupStatePtr& ptr, uint16_t nid) {
    zx_status_t res;

    RUN_COMMAND_LIST(ptr, nid, FETCH_FUNCTION_GROUP_TYPE,
                    "Failed to fetch function group type (nid %hu)", nid);

    if (ptr->can_send_unsolicited_) {
        RUN_COMMAND_LIST(ptr->unsol_resp_ctrl_, nid, FETCH_UNSOLICITED_RESPONSE_STATE,
                        "Failed to fetch unsolicited response control state (nid %hu)", nid);
    }

    ptr->nid_ = nid;

    switch (ptr->type_) {
    case FunctionGroupState::Type::AUDIO: {
        auto& afg = *(static_cast<AudioFunctionGroupState*>(ptr.get()));
        return ReadAudioFunctionGroupState(afg);
    }

    case FunctionGroupState::Type::MODEM: {
        // We do not support probing the state of modem function groups right now.
        printf("Warning: MODEM function group (nid %hd) state details not fetched.\n", nid);
        break;
    }

    default:
        // ParseFnGroupType should have aborted at this point if the function
        // group type was not valid.
        ZX_DEBUG_ASSERT((ptr->type_ >= FunctionGroupState::Type::VENDOR_START) &&
                        (ptr->type_ <= FunctionGroupState::Type::VENDOR_END));
        break;
    }

    return ZX_OK;
}

zx_status_t IntelHDACodec::ReadAudioFunctionGroupState(AudioFunctionGroupState& afg) {
    zx_status_t res;

    RUN_COMMAND_LIST(afg, afg.nid_, FETCH_AFG_PROPERTIES,
                    "Failed to audio fn group properties (nid %hu)", afg.nid_);

    RUN_COMMAND_LIST(afg.power_, afg.nid_, FETCH_POWER_STATE,
                     "Failed to fetch Power caps/state for audio function group (nid %hu)",
                     afg.nid_);

    for (uint32_t i = 0; i < afg.widget_count_; ++i) {
        auto& widget_ptr = afg.widgets_[i];
        uint16_t nid = static_cast<uint16_t>(afg.widget_starting_id_ + i);

        RUN_COMMAND_LIST(widget_ptr, nid, FETCH_WIDGET_TYPE,
                        "Failed to audio widget type (nid %hu) for function "
                        "group located at nid %hu", nid, afg.nid_);

        widget_ptr->nid_ = nid;
        widget_ptr->afg_ = &afg;

        res = ReadAudioWidgetState(*widget_ptr);
        if (res != ZX_OK)
            return res;
    }

    return ZX_OK;
}

zx_status_t IntelHDACodec::ReadAudioWidgetState(AudioWidgetState& widget) {
    zx_status_t res;

    switch (widget.caps_.type()) {
    case AudioWidgetCaps::Type::INPUT:
        RUN_COMMAND_LIST(widget, widget.nid_, FETCH_AUDIO_INPUT_CAPS,
                         "Failed to fetch INPUT_CAPS for audio widget (nid %hu)",
                         widget.nid_);
        break;

    case AudioWidgetCaps::Type::OUTPUT:
        RUN_COMMAND_LIST(widget, widget.nid_, FETCH_AUDIO_OUTPUT_CAPS,
                         "Failed to fetch OUTPUT_CAPS for audio widget (nid %hu)",
                         widget.nid_);
        break;

    case AudioWidgetCaps::Type::PIN_COMPLEX:
        if (widget.caps_.digital()) {
            RUN_COMMAND_LIST(widget, widget.nid_, FETCH_DIGITAL_PIN_COMPLEX_CAPS,
                             "Failed to fetch DIGITAL_PIN_COMPLEX_CAPS for audio widget "
                             "(nid %hu)", widget.nid_);
        } else {
            RUN_COMMAND_LIST(widget, widget.nid_, FETCH_NON_DIGITAL_PIN_COMPLEX_CAPS,
                             "Failed to fetch NON_DIGITAL_PIN_COMPLEX_CAPS for audio widget "
                             "(nid %hu)", widget.nid_);
        }
        break;

    case AudioWidgetCaps::Type::MIXER:
        RUN_COMMAND_LIST(widget, widget.nid_, FETCH_MIXER_CAPS,
                         "Failed to fetch MIXER_CAPS for audio widget (nid %hu)",
                         widget.nid_);
        break;

    case AudioWidgetCaps::Type::SELECTOR:
        RUN_COMMAND_LIST(widget, widget.nid_, FETCH_SELECTOR_CAPS,
                         "Failed to fetch SELECTOR_CAPS for audio widget (nid %hu)",
                         widget.nid_);
        break;

    case AudioWidgetCaps::Type::POWER:
        RUN_COMMAND_LIST(widget, widget.nid_, FETCH_POWER_CAPS,
                         "Failed to fetch POWER_CAPS for audio widget (nid %hu)",
                         widget.nid_);
        break;

    case AudioWidgetCaps::Type::VOLUME_KNOB:
        RUN_COMMAND_LIST(widget, widget.nid_, FETCH_VOLUME_KNOB_CAPS,
                         "Failed to fetch VOLUME_KNOB_CAPS for audio widget (nid %hu)",
                         widget.nid_);
        break;

    // We don't currently fetch any state for beep generators or vendor widgets.
    case AudioWidgetCaps::Type::BEEP_GEN:
    case AudioWidgetCaps::Type::VENDOR:
        break;

    default:
        printf("Unrecognized audio widget type (%u) at nid %hu\n",
                static_cast<uint32_t>(widget.caps_.type()), widget.nid_);
        return ZX_ERR_BAD_STATE;
    }

    // If this widget has a connection list, read it now.
    if (widget.caps_.has_conn_list()) {
        res = ReadConnList(widget);
        if (res != ZX_OK)
            return res;
    }

    // If this widget has power management capabilities, read the caps and the
    // current state now.
    if (widget.caps_.has_power_ctl()) {
        RUN_COMMAND_LIST(widget.power_, widget.nid_, FETCH_POWER_STATE,
                         "Failed to fetch Power caps/state for audio widget (nid %hu)",
                         widget.nid_);

        // From section 7.3.4.12.
        //
        // "If this is not implemented (returns 0's) or just returns 0 as
        // response to reading this parameter for a node that supports a Power
        // State Control (see section 7.3.3.10) then the supported power states
        // for that node will be the same as reported for the Function Group."
        if (widget.power_.supported_states_ == 0) {
            ZX_DEBUG_ASSERT(widget.afg_ != nullptr);
            widget.power_.supported_states_ = widget.afg_->power_.supported_states_;
        }
    }

    // If this is an input or output converter widget, read the currently configured format.
    if ((widget.caps_.type() == AudioWidgetCaps::Type::INPUT) ||
        (widget.caps_.type() == AudioWidgetCaps::Type::OUTPUT)) {
        CodecResponse resp;

        res = DoCodecCmd(widget.nid_, GET_CONVERTER_FORMAT, &resp);
        if (res != ZX_OK) {
            printf("Failed to get stream converter format for for nid %hu (res %d)\n",
                    widget.nid_, res);
            return res;
        }

        widget.cur_format_.raw_data_ = static_cast<uint16_t>(resp.data & 0xFFFF);
    }

    // If this is a pin complex, and it supports presence detection, and the
    // JackOverride bit has not been set in the config defaults, query the pin
    // sense.
    if ((widget.caps_.type() == AudioWidgetCaps::Type::PIN_COMPLEX) &&
        (widget.pin_caps_ & AW_PIN_CAPS_FLAG_CAN_PRESENCE_DETECT) &&
        (!widget.cfg_defaults_.jack_detect_override())) {

        // TODO(johngro): Add support for SW triggering a pin detection.  Timing
        // requirements are unclear and may be codec specific.  Also, triggering
        // the presence detection is a "set" operation, which is not currently
        // permitted by the driver.
        if (widget.pin_caps_ & AW_PIN_CAPS_FLAG_TRIGGER_REQUIRED) {
            printf("WARNING: SW triggered presence sensing not supported (nid %hu)\n",
                    widget.nid_);
        } else {
            // TODO(johngro): do we need to bring the pin complex to a
            // particular power state in order for presence detect to work, or
            // should it run at all power states?
            CodecResponse resp;
            res = DoCodecCmd(widget.nid_, GET_PIN_SENSE, &resp);
            if (res != ZX_OK) {
                printf("Failed to get pin sense status for pin complex nid %hu (res %d)\n",
                        widget.nid_, res);
                return res;
            }

            widget.pin_sense_.raw_data_ = resp.data;
            widget.pin_sense_valid_ = true;
        }
    }

    // Read the current state of the EAPD/BTL register if this is...
    //
    // 1) A pin complex with external amplifier control.
    // 2) A pin complex capable of balanced output.
    // 3) Any widget capable of swapping L/R channels
    if (widget.caps_.can_lr_swap() ||
       (widget.pin_caps_ & AW_PIN_CAPS_FLAG_BALANCED_IO) ||
       (widget.pin_caps_ & AW_PIN_CAPS_FLAG_CAN_EAPD)) {
        CodecResponse resp;
        res = DoCodecCmd(widget.nid_, GET_EAPD_BTL_ENABLE, &resp);
        if (res != ZX_OK) {
            printf("Failed to get EAPD/BTL state for nid %hu (res %d)\n",
                    widget.nid_, res);
            return res;
        }

        widget.eapd_state_.raw_data_ = resp.data;
    }

    // If this widget has an input or output amplifier, read its current state.
    //
    // Todo(johngro) : add support for reading gain settings for mixers and
    // summing widgets which have more than just a single amplifier gain/mute
    // setting.
    if (widget.caps_.input_amp_present()) {
        // If this a mixer, read the individual input amp state for each of the mixer inputs.
        // Otherwise, just read the common input amp state.
        if (widget.caps_.type() == AudioWidgetCaps::Type::MIXER) {
            for (uint8_t i = 0; i < widget.conn_list_len_; ++i) {
                res = ReadAmpState(widget.nid_, true, i,
                                   widget.input_amp_caps_,
                                   &widget.conn_list_[i].amp_state_);
                if (res != ZX_OK)
                    return res;
            }
        } else {
            res = ReadAmpState(widget.nid_, true, 0,
                               widget.input_amp_caps_, &widget.input_amp_state_);
            if (res != ZX_OK)
                return res;
        }
    }

    if (widget.caps_.output_amp_present()) {
        res = ReadAmpState(widget.nid_, false, 0,
                           widget.output_amp_caps_, &widget.output_amp_state_);
        if (res != ZX_OK)
            return res;
    }

    // If this widget can send unsolicited responses, query the current state of
    // the unsolicted response controls.
    if (widget.caps_.can_send_unsol()) {
        RUN_COMMAND_LIST(widget.unsol_resp_ctrl_, widget.nid_,
                         FETCH_UNSOLICITED_RESPONSE_STATE,
                         "Failed to fetch unsolicited response control state (nid %hu)",
                         widget.nid_);
    }

    // Finished.
    return ZX_OK;
}

#undef RUN_COMMAND_LIST

zx_status_t IntelHDACodec::ReadConnList(AudioWidgetState& widget) {
    CodecResponse resp;
    zx_status_t   res;

    ZX_DEBUG_ASSERT(widget.conn_list_len_ > 0);
    ZX_DEBUG_ASSERT(widget.conn_list_ != nullptr);

    size_t i = 0;
    while (i < widget.conn_list_len_) {
        res = DoCodecCmd(widget.nid_, GET_CONNECTION_LIST_ENTRY(static_cast<uint8_t>(i)), &resp);
        if (res != ZX_OK) {
            printf("Failed to get connection list entry at ndx %zu for nid %hu (res %d)\n",
                    i, widget.nid_, res);
            return res;
        }

        // See section 7.1.2 and figure 51 for the format of long and short form
        // connection widget.conn_list_ entries.
        if (widget.long_form_conn_list_) {
            for (size_t j = 0; (j < 2) && (i < widget.conn_list_len_); j++, i++) {
                uint16_t raw = static_cast<uint16_t>(resp.data & 0xFFFF);
                widget.conn_list_[i].range_ = (raw & 0x8000u) != 0;
                widget.conn_list_[i].nid_   = (raw & 0x7FFFu);
                resp.data >>= 16;
            }
        } else {
            for (size_t j = 0; (j < 4) && (i < widget.conn_list_len_); j++, i++) {
                uint8_t raw = static_cast<uint8_t>(resp.data & 0xFF);
                widget.conn_list_[i].range_ = (raw & 0x80u) != 0;
                widget.conn_list_[i].nid_   = (raw & 0x7Fu);
                resp.data >>= 8;
            }
        }
    }

    // Sanity check the widget.conn_list_.
    for (i = 0; i < widget.conn_list_len_; ++i) {
        if (widget.conn_list_[i].range_ && (!i || widget.conn_list_[i-1].range_)) {
            printf("Invalid connection widget.conn_list_ entry [nid, ndx] = [%hu, %zu]. "
                   "Range end may not be the first entry in the connection widget.conn_list_, "
                   "or proceeded by a range end entry.\n",
                   widget.nid_, i);
            return ZX_ERR_BAD_STATE;
        }
    }

    // If the connection list length is greater than 1, and this is not a mixer
    // widger, then there exists a selection control.  Read it's current setting
    // so we can report it.  Otherwise, the currently connected NID must be the
    // same as the first entry in the list, or this is a mixer widget in which
    // case it is always connected to all of the entries in the connection list.
    if (widget.caps_.type() != AudioWidgetCaps::Type::MIXER) {
        if (widget.conn_list_len_ == 1) {
            widget.connected_nid_ = widget.conn_list_[0].nid_;
            widget.connected_nid_ndx_ = 0;
        } else {
            // Select control response format documented in section 7.3.3.2 Table 73
            res = DoCodecCmd(widget.nid_, GET_CONNECTION_SELECT_CONTROL, &resp);
            if (res != ZX_OK) {
                printf("Failed to get connection selection for nid %hu (res %d)\n",
                        widget.nid_, res);
                return res;
            }

            widget.connected_nid_ndx_ = static_cast<uint8_t>(resp.data & 0xFF);
            widget.connected_nid_ = (widget.connected_nid_ndx_ < widget.conn_list_len_)
                                  ?  widget.conn_list_[widget.connected_nid_ndx_].nid_
                                  : 0;
        }
    } else {
        widget.connected_nid_ = 0;
        widget.connected_nid_ndx_ = 0;
    }

    return ZX_OK;
}

zx_status_t IntelHDACodec::ReadAmpState(uint16_t nid, bool is_input, uint8_t ndx,
                                        const AmpCaps& caps,
                                        AudioWidgetState::AmpState* state_out) {
    ZX_DEBUG_ASSERT(state_out);

    CodecResponse resp;
    zx_status_t   res;

    for (size_t i = 0; i < countof(state_out->gain); ++i) {
        res = DoCodecCmd(nid, GET_AMPLIFIER_GAIN_MUTE(is_input, (i > 0), ndx), &resp);
        if (res != ZX_OK) {
            printf("Failed to get amp settings for nid %hu's %s %s amplifier #%u (res %d)\n",
                    nid,
                    (i > 0)  ? "right" : "left",
                    is_input ? "input" : "output",
                    ndx, res);
            return res;
        }

        // Section 7.3.3.7 and Figure 62
        state_out->gain[i] = static_cast<uint8_t>(resp.data & 0x7f);
        state_out->mute[i] = (resp.data & 0x80) != 0;
    }

    return ZX_OK;
}

zx_status_t IntelHDACodec::DoCodecCmd(uint16_t nid,
                                      const CodecVerb& verb,
                                      CodecResponse* resp_out) {

    ZX_DEBUG_ASSERT(resp_out != nullptr);

    ihda_codec_send_corb_cmd_req_t  req;
    ihda_codec_send_corb_cmd_resp_t resp;

    InitRequest(&req, IHDA_CODEC_SEND_CORB_CMD);
    req.nid  = nid;
    req.verb = verb.val;

    zx_status_t res = CallDevice(req, &resp);
    if (res != ZX_OK) {
        printf("Codec command failed; [nid, verb] = [%2u, 0x%05x] (res %d)\n", nid, verb.val, res);
        return res;
    }

    resp_out->data    = resp.data;
    resp_out->data_ex = resp.data_ex;

    return ZX_OK;
}

template <typename T>
zx_status_t IntelHDACodec::RunCommandList(T& target,
                                          uint16_t nid,
                                          const CommandListEntry<T>* cmds,
                                          size_t cmd_count) {
    ZX_DEBUG_ASSERT(cmds);

    for (size_t i = 0; i < cmd_count; ++i) {
        const auto& cmd = cmds[i];
        zx_status_t res;
        CodecResponse resp;

        res = DoCodecCmd(nid, cmd.verb, &resp);
        if (res != ZX_OK)
            return res;

        res = cmd.parser(target, resp);
        if (res != ZX_OK) {
            printf("Cmd parse; [nid, verb] = [%2u, 0x%05x] --> resp [0x%08x, 0x%08x] (res %d)\n",
                    nid, cmd.verb.val, resp.data, resp.data_ex, res);
            return res;
        }
    }

    return ZX_OK;
}

}  // namespace audio
}  // namespace intel_hda
