// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include "codec_state.h"

namespace audio {
namespace intel_hda {

// Don't allow this code to pollute the rest of the namespace.
namespace {

const char* ToString(const FunctionGroupState::Type& val) {
    switch (val) {
        case FunctionGroupState::Type::AUDIO: return "AUDIO";
        case FunctionGroupState::Type::MODEM: return "MODEM";
        default:
            if ((val >= FunctionGroupState::Type::VENDOR_START) &&
                (val <= FunctionGroupState::Type::VENDOR_END))
                return "VENDOR";
            else
                return "<unknown>";
    }
}

const char* ToString(const AudioWidgetCaps::Type& val) {
    switch (val) {
        case AudioWidgetCaps::Type::OUTPUT:      return "OUTPUT";
        case AudioWidgetCaps::Type::INPUT:       return "INPUT";
        case AudioWidgetCaps::Type::MIXER:       return "MIXER";
        case AudioWidgetCaps::Type::SELECTOR:    return "SELECTOR";
        case AudioWidgetCaps::Type::PIN_COMPLEX: return "PIN_COMPLEX";
        case AudioWidgetCaps::Type::POWER:       return "POWER";
        case AudioWidgetCaps::Type::VOLUME_KNOB: return "VOLUME_KNOB";
        case AudioWidgetCaps::Type::BEEP_GEN:    return "BEEP_GEN";
        case AudioWidgetCaps::Type::VENDOR:      return "VENDOR";
        default:                                  return "<unknown>";
    }
}

const char* PowerStateToString(uint8_t val) {
    switch (val) {
        case 0u: return "D0";
        case 1u: return "D1";
        case 2u: return "D2";
        case 3u: return "D3HOT";
        case 4u: return "D3COLD";
        default: return "Unknown";
    };
}

void Dump(const AmpCaps& caps, const AudioWidgetState::AmpState* amp_state = nullptr) {
    float start = 0.0;
    float stop  = 0.0;
    float step  = 0.0;

    if (!caps.step_size() || !caps.num_steps()) {
        printf("none\n");
        return;
    } else if (caps.num_steps() == 1) {
        printf("fixed 0 dB gain");
    } else {
        step  = (static_cast<float>(caps.step_size()) / 4.0f);
        start = -static_cast<float>(caps.offset()) * step;
        stop  = start + ((static_cast<float>(caps.num_steps()) - 1) * step);
        printf("[%.2f, %.2f] dB in %.2f dB steps", start, stop, step);
    }

    printf(" (Can%s mute)", caps.can_mute() ? "" : "'t");

    if (amp_state != nullptr) {
        printf(" [");
        for (size_t i = 0; i < countof(amp_state->gain); ++i) {
            if (i) printf(", ");
            printf("%c:", !i ? 'L' : 'R');

            if (caps.can_mute() && amp_state->mute[i]) {
                printf("mute");
            } else {
                printf("%.2f dB", start + (step * amp_state->gain[0]));
            }
        }

        printf("]");
    }

    printf("\n");
}

void Dump(const AudioWidgetState::StreamFormat& format) {
    if (!format.is_pcm()) {
        printf("Non-PCM (raw 0x%04hx)\n", format.raw_data_);
        return;
    }

    printf("%u chan %u Hz %u bps (raw 0x%04hx)\n",
        format.channels(),
        format.sample_rate(),
        format.bits_per_chan(),
        format.raw_data_);
}

#define FMT(fmt) "%s%17s : " fmt, pad
void Dump(const ConfigDefaults& cfg) {
    static const char* pad = "+     \\-- ";
    const char *tmp, *tmp2;

    // Table 109
    switch (cfg.port_connectivity()) {
    case 0:  tmp = "Jack"; break;
    case 1:  tmp = "Unconnected"; break;
    case 2:  tmp = "Integrated"; break;
    case 3:  tmp = "Jack+Integrated"; break;
    default: tmp = "ERROR"; break;
    }

    printf(FMT("%s (%u)\n"), "Port Connectivity", tmp, cfg.port_connectivity());

    // Table 110
    uint32_t loc1 =  cfg.location() & 0xF;
    uint32_t loc2 = (cfg.location() >> 4) & 0x3;
    switch (loc1) {
    case 0:  tmp = "N/A"; break;
    case 1:  tmp = "Rear"; break;
    case 2:  tmp = "Front"; break;
    case 3:  tmp = "Left"; break;
    case 4:  tmp = "Right"; break;
    case 5:  tmp = "Top"; break;
    case 6:  tmp = "Bottom"; break;
    case 7:
    case 8:
    case 9:  tmp = "Special"; break;
    default: tmp = "Unknown"; break;
    }

    switch (loc2) {
    case 0:  tmp2 = " External"; break;
    case 1:  tmp2 = " Internal"; break;
    case 2:  tmp2 = " Separate Chassis"; break;
    case 3:  tmp2 = " Other"; break;
    default: tmp2 = " ERROR"; break;
    }

    switch (cfg.location()) {
    case 0x07: tmp2 = ""; tmp = "Rear Panel"; break;
    case 0x08: tmp2 = ""; tmp = "Drive Bay"; break;
    case 0x17: tmp2 = ""; tmp = "Riser"; break;
    case 0x18: tmp2 = ""; tmp = "Digital Display"; break;
    case 0x19: tmp2 = ""; tmp = "ATAPI"; break;
    case 0x37: tmp2 = ""; tmp = "Mobile Lid - Inside"; break;
    case 0x38: tmp2 = ""; tmp = "Mobile Lid - Outside"; break;
    default: break;
    }

    printf(FMT("%s%s (0x%02x)\n"), "Location", tmp, tmp2, cfg.location());

    // Table 111
    switch (cfg.default_device()) {
    case 0x0: tmp = "Line Out"; break;
    case 0x1: tmp = "Speaker"; break;
    case 0x2: tmp = "Headphone Out"; break;
    case 0x3: tmp = "CD"; break;
    case 0x4: tmp = "S/PDIF Out"; break;
    case 0x5: tmp = "Digital Other Out"; break;
    case 0x6: tmp = "Modem Line Side"; break;
    case 0x7: tmp = "Modem Handset Side"; break;
    case 0x8: tmp = "Line In"; break;
    case 0x9: tmp = "AUX"; break;
    case 0xa: tmp = "Mic In"; break;
    case 0xb: tmp = "Telephony"; break;
    case 0xc: tmp = "S/PDIF In"; break;
    case 0xd: tmp = "Digital Other In"; break;
    case 0xf: tmp = "Other"; break;
    default:  tmp = "Unknown"; break;
    }

    printf(FMT("%s (%u)\n"), "Default Device", tmp, cfg.default_device());

    // Table 112
    switch (cfg.connection_type()) {
    case 0x1: tmp = "1/8 inch"; break;
    case 0x2: tmp = "1/4 inch"; break;
    case 0x3: tmp = "ATAPI Internal"; break;
    case 0x4: tmp = "RCA"; break;
    case 0x5: tmp = "Optical"; break;
    case 0x6: tmp = "Other Digital"; break;
    case 0x7: tmp = "Other Analog"; break;
    case 0x8: tmp = "Multichannel Analog (DIN)"; break;
    case 0x9: tmp = "XLR/Pro"; break;
    case 0xa: tmp = "RJ-11 (Modem)"; break;
    case 0xb: tmp = "Combination"; break;
    case 0xf: tmp = "Other"; break;
    default:  tmp = "Unknown"; break;
    }

    printf(FMT("%s (%u)\n"), "Connection Type", tmp, cfg.connection_type());

    // Table 113
    switch (cfg.color()) {
    case 0x1: tmp = "Black"; break;
    case 0x2: tmp = "Grey"; break;
    case 0x3: tmp = "Blue"; break;
    case 0x4: tmp = "Green"; break;
    case 0x5: tmp = "Red"; break;
    case 0x6: tmp = "Orange"; break;
    case 0x7: tmp = "Yellow"; break;
    case 0x8: tmp = "Purple"; break;
    case 0x9: tmp = "Pink"; break;
    case 0xe: tmp = "White"; break;
    case 0xf: tmp = "Other"; break;
    default:  tmp = "Unknown"; break;
    }

    printf(FMT("%s (%u)\n"), "Color", tmp, cfg.color());

    // Associations and Flags
    printf(FMT("Assoc Group (%u) Assoc Seq (%u)%s\n"), "Assoc/Flags",
            cfg.default_assoc(),
            cfg.sequence(),
            cfg.misc() & 0x1 ? " JackDetectOverride" : "");

}
#undef FMT

typedef struct flag_lut_entry {
    uint32_t    flag_bit;
    const char* flag_name;
} flag_lut_entry_t;

static void ihda_dump_delay(uint8_t delay) {
    if (delay)
        printf("%u samples\n", delay);
    else
        printf("unknown\n");
}

static void ihda_dump_flags(uint32_t flags,
                            const flag_lut_entry_t* table,
                            size_t table_size,
                            const char* suffix,
                            const char* no_flags_text) {
    bool got_one = false;
    for (size_t i = 0; i < table_size; ++i) {
        if (flags & table[i].flag_bit) {
            printf("%s%s", got_one ? " " : "", table[i].flag_name);
            got_one = true;
        }
    }

    printf("%s\n", got_one ? suffix : no_flags_text);
}

static const flag_lut_entry_t POWER_STATE_FLAGS[] = {
    { IHDA_PWR_STATE_EPSS,     "EPSS" },
    { IHDA_PWR_STATE_CLKSTOP,  "CLKSTOP" },
    { IHDA_PWR_STATE_S3D3COLD, "S3D3COLD" },
    { IHDA_PWR_STATE_D3COLD,   "D3COLD" },
    { IHDA_PWR_STATE_D3,       "D3HOT" },
    { IHDA_PWR_STATE_D2,       "D2" },
    { IHDA_PWR_STATE_D1,       "D1" },
    { IHDA_PWR_STATE_D0,       "D0" },
};

static const flag_lut_entry_t PCM_RATE_FLAGS[] = {
    { IHDA_PCM_RATE_384000, "384000" },
    { IHDA_PCM_RATE_192000, "192000" },
    { IHDA_PCM_RATE_176400, "176400" },
    { IHDA_PCM_RATE_96000,   "96000" },
    { IHDA_PCM_RATE_88200,   "88200" },
    { IHDA_PCM_RATE_48000,   "48000" },
    { IHDA_PCM_RATE_44100,   "44100" },
    { IHDA_PCM_RATE_32000,   "32000" },
    { IHDA_PCM_RATE_22050,   "22050" },
    { IHDA_PCM_RATE_16000,   "16000" },
    { IHDA_PCM_RATE_11025,   "11025" },
    { IHDA_PCM_RATE_8000,     "8000" },
};

static const flag_lut_entry_t PCM_SIZE_FLAGS[] = {
    { IHDA_PCM_SIZE_32BITS, "32" },
    { IHDA_PCM_SIZE_24BITS, "24" },
    { IHDA_PCM_SIZE_20BITS, "20" },
    { IHDA_PCM_SIZE_16BITS, "16" },
    { IHDA_PCM_SIZE_8BITS,   "8" },
};

static const flag_lut_entry_t PCM_FMT_FLAGS[] = {
    { IHDA_PCM_FORMAT_AC3,     "AC3" },
    { IHDA_PCM_FORMAT_FLOAT32, "FLOAT32" },
    { IHDA_PCM_FORMAT_PCM,     "PCM" },
};

static const flag_lut_entry_t AW_CAPS_FLAGS[] = {
    { AudioWidgetCaps::FLAG_AMP_PARAM_OVERRIDE, "AmpParamOverride" },
    { AudioWidgetCaps::FLAG_FORMAT_OVERRIDE,    "FormatOverride" },
    { AudioWidgetCaps::FLAG_STRIPE_SUPPORTED,   "StripingSupported" },
    { AudioWidgetCaps::FLAG_PROC_WIDGET,        "HasProcessingControls" },
    { AudioWidgetCaps::FLAG_CAN_SEND_UNSOL,     "CanSendUnsolicited" },
    { AudioWidgetCaps::FLAG_DIGITAL,            "Digital" },
    { AudioWidgetCaps::FLAG_CAN_LR_SWAP,        "CanSwapLR" },
    { AudioWidgetCaps::FLAG_HAS_CONTENT_PROT,   "HasContentProtection" },
};

static const flag_lut_entry_t PIN_CAPS_FLAGS[] = {
    { AW_PIN_CAPS_FLAG_CAN_IMPEDANCE_SENSE,  "ImpedanceSense" },
    { AW_PIN_CAPS_FLAG_TRIGGER_REQUIRED,     "TrigReq" },
    { AW_PIN_CAPS_FLAG_CAN_PRESENCE_DETECT,  "PresDetect" },
    { AW_PIN_CAPS_FLAG_CAN_DRIVE_HEADPHONES, "HeadphoneDrive" },
    { AW_PIN_CAPS_FLAG_CAN_OUTPUT,           "CanOutput" },
    { AW_PIN_CAPS_FLAG_CAN_INPUT,            "CanInput" },
    { AW_PIN_CAPS_FLAG_BALANCED_IO,          "Balanced" },
    { AW_PIN_CAPS_FLAG_HDMI,                 "HDMI" },
    { AW_PIN_CAPS_FLAG_VREF_HIZ,             "VREF_HIZ" },
    { AW_PIN_CAPS_FLAG_VREF_50_PERCENT,      "VREF_50%" },
    { AW_PIN_CAPS_FLAG_VREF_GROUND,          "VREF_GND" },
    { AW_PIN_CAPS_FLAG_VREF_80_PERCENT,      "VREF_80%" },
    { AW_PIN_CAPS_FLAG_VREF_100_PERCENT,     "VREF_100%" },
    { AW_PIN_CAPS_FLAG_CAN_EAPD,             "EAPD" },
    { AW_PIN_CAPS_FLAG_DISPLAY_PORT,         "DisplayPort" },
    { AW_PIN_CAPS_FLAG_HIGH_BIT_RATE,        "HighBitRate" },
};

#define DUMP_FLAGS(flags, table, suffix, no_flags_text) \
    ihda_dump_flags(flags, table, countof(table), suffix, no_flags_text)

static void ihda_dump_conn_list(const AudioWidgetState& widget) {
    if (!widget.conn_list_len_) {
        printf("empty\n");
        return;
    }

    ZX_DEBUG_ASSERT(widget.conn_list_);
    for (uint32_t i = 0; i < widget.conn_list_len_; ++i) {
        if(i > 0)
            printf(" ");

        const auto& first = widget.conn_list_[i];
        ZX_DEBUG_ASSERT(!first.range_);

        // Is this entry the start of a range or not?
        if ((i + 1) < widget.conn_list_len_) {
            const auto& second = widget.conn_list_[i + 1];
            if (second.range_) {
                printf("[%hu, %hu]", first.nid_, second.nid_);
                i++;
                continue;
            }
        }

        printf("%hu", first.nid_);
    }

    // Mixers are always connected to all of the inputs on their connection lists.
    if (widget.caps_.type() != AudioWidgetCaps::Type::MIXER) {
        if (widget.connected_nid_ndx_ < widget.conn_list_len_) {
            printf(" : [*%hu, ndx %u]\n", widget.connected_nid_, widget.connected_nid_ndx_);
        } else {
            printf(" : [*INVALID, ndx %u]\n", widget.connected_nid_ndx_);
        }
    } else {
        printf("\n");
    }

}

#define FMT(fmt) "%s%20s : " fmt, pad
static void ihda_dump_widget(const AudioWidgetState& widget, uint32_t id, uint32_t count) {
    static const char* pad = "+----- ";

    printf("%sWidget %u/%u\n", pad, id, count);
    printf(FMT("%hu\n"), "Node ID", widget.nid_);
    printf(FMT("[%02x] %s\n"),  "Type",
            static_cast<uint32_t>(widget.caps_.type()),
            ToString(widget.caps_.type()));

    printf(FMT("%08x\n"), "Raw Caps", widget.caps_.raw_data_);

    printf(FMT(""), "Flags");
    DUMP_FLAGS(widget.caps_.raw_data_, AW_CAPS_FLAGS, "", "none");

    if (widget.caps_.can_send_unsol()) {
        printf(FMT("%s [tag 0x%02x]\n"),  "Unsolicited Ctrl",
                   widget.unsol_resp_ctrl_.enabled() ? "enabled" : "disabled",
                   widget.unsol_resp_ctrl_.tag());
    }

    printf(FMT(""), "Delay");
    ihda_dump_delay(widget.caps_.delay());

    printf(FMT("%u\n"), "MaxChan", widget.caps_.ch_count());

    if (widget.caps_.input_amp_present()) {
        if (widget.caps_.type() != AudioWidgetCaps::Type::MIXER) {
            printf(FMT(""), "InputAmp");
            Dump(widget.input_amp_caps_, &widget.input_amp_state_);
        } else {
            for (uint8_t i = 0; i < widget.conn_list_len_; ++i) {
                char tag[32];
                snprintf(tag, countof(tag), "InputAmp[nid %hu]", widget.conn_list_[i].nid_);
                printf(FMT(""), tag);
                Dump(widget.input_amp_caps_, &widget.conn_list_[i].amp_state_);
            }
        }
    }

    if (widget.caps_.output_amp_present()) {
        printf(FMT(""), "OutputAmp");
        Dump(widget.output_amp_caps_, &widget.output_amp_state_);
    }

    if (widget.caps_.format_override()) {
        printf(FMT(""), "PCM Rates");
        DUMP_FLAGS(widget.pcm_size_rate_, PCM_RATE_FLAGS, "", "none");

        printf(FMT(""), "PCM Sizes");
        DUMP_FLAGS(widget.pcm_size_rate_, PCM_SIZE_FLAGS, " bits", "none");

        printf(FMT(""), "PCM Formats");
        DUMP_FLAGS(widget.pcm_formats_, PCM_FMT_FLAGS, "", "none");
    }

    if ((widget.caps_.type() == AudioWidgetCaps::Type::INPUT) ||
        (widget.caps_.type() == AudioWidgetCaps::Type::OUTPUT)) {
        printf(FMT(""), "Cur Format");
        Dump(widget.cur_format_);
        printf(FMT("tag (%u) chan (%u)\n"), "Tag/Chan", widget.stream_tag_, widget.stream_chan_);
    }

    if (widget.caps_.type() == AudioWidgetCaps::Type::PIN_COMPLEX) {
        if (widget.pin_sense_valid_) {
            const char* pstring = widget.pin_sense_.presence_detect() ? "Plugged" : "Unplugged";
            if (widget.caps_.digital()) {
                printf(FMT("%s, ELD %s [raw 0x%08x]\n"), "Pin Sense",
                        pstring,
                        widget.pin_sense_.eld_valid() ? "Valid" : "Invalid",
                        widget.pin_sense_.raw_data_);
            } else {
                if (widget.pin_caps_ & AW_PIN_CAPS_FLAG_CAN_IMPEDANCE_SENSE) {
                    printf(FMT("%s, Impedance %u [raw 0x%08x]\n"), "Pin Sense",
                            pstring, widget.pin_sense_.impedance(), widget.pin_sense_.raw_data_);
                } else {
                    printf(FMT("%s [raw 0x%08x]\n"), "Pin Sense",
                            pstring, widget.pin_sense_.raw_data_);
                }
            }
        }

        printf(FMT(""), "Pin Caps");
        DUMP_FLAGS(widget.pin_caps_, PIN_CAPS_FLAGS, "", "none");
    }

   if (widget.caps_.can_lr_swap())
       printf(FMT("%s\n"), "L/R Swap", widget.eapd_state_.lr_swap() ? "Swapped" : "Normal");

   if (widget.caps_.type() == AudioWidgetCaps::Type::PIN_COMPLEX) {
       if (widget.pin_caps_ & AW_PIN_CAPS_FLAG_CAN_INPUT)
           printf(FMT("%s\n"), "Input",
                   widget.pin_widget_ctrl_.input_enb() ? "Enabled" : "Disabled");

       if (widget.pin_caps_ & AW_PIN_CAPS_FLAG_CAN_OUTPUT)
           printf(FMT("%s\n"), "Output",
                   widget.pin_widget_ctrl_.output_enb() ? "Enabled" : "Disabled");

       if (widget.pin_caps_ & AW_PIN_CAPS_FLAG_CAN_DRIVE_HEADPHONES)
           printf(FMT("%s\n"), "Headphone Amp",
                   widget.pin_widget_ctrl_.hp_amp_enb() ? "Enabled" : "Disabled");

       if (!widget.caps_.digital() &&
           (widget.pin_caps_ & (AW_PIN_CAPS_FLAG_VREF_HIZ         |
                                AW_PIN_CAPS_FLAG_VREF_50_PERCENT  |
                                AW_PIN_CAPS_FLAG_VREF_GROUND      |
                                AW_PIN_CAPS_FLAG_VREF_80_PERCENT  |
                                AW_PIN_CAPS_FLAG_VREF_100_PERCENT))) {
           const char* tmp;
           switch (widget.pin_widget_ctrl_.vref_enb()) {
           case VRefEn::HiZ:  tmp = "Hi-Z";     break;
           case VRefEn::P50:  tmp = "50%";      break;
           case VRefEn::Gnd:  tmp = "Grounded"; break;
           case VRefEn::P80:  tmp = "80%";      break;
           case VRefEn::P100: tmp = "100%";     break;
           default:           tmp = "Unknown";  break;
           }
           printf(FMT("%s\n"), "VRef", tmp);
       }

       if (widget.caps_.digital()) {
           const char* tmp;
           switch (widget.pin_widget_ctrl_.ept()) {
           case EPT::Native: tmp = "Native";        break;
           case EPT::HBR:    tmp = "High Bit Rate"; break;
           default:          tmp = "Unknown";       break;
           }
           printf(FMT("%s\n"), "Encoded Pkt Type", tmp);
       }

       if (widget.pin_caps_ & AW_PIN_CAPS_FLAG_BALANCED_IO)
           printf(FMT("%s\n"), "Balanced Output", widget.eapd_state_.btl() ? "Yes" : "No");

       if (widget.pin_caps_ & AW_PIN_CAPS_FLAG_CAN_EAPD)
           printf(FMT("Powered %s\n"), "External Amp", widget.eapd_state_.eapd() ? "Up" : "Down");

        printf(FMT("0x%08x\n"), "Raw Cfg Defaults", widget.cfg_defaults_.raw_data_);
        Dump(widget.cfg_defaults_);
    }

    if (widget.caps_.has_power_ctl()) {
        printf(FMT(""), "Sup. Pwr States");
        DUMP_FLAGS(widget.power_.supported_states_, POWER_STATE_FLAGS, "", "none");
        printf(FMT("Set %s(%u) Active %s(%u)%s%s%s\n"), "Cur Pwr State",
                    PowerStateToString(widget.power_.set_), widget.power_.set_,
                    PowerStateToString(widget.power_.active_), widget.power_.active_,
                    widget.power_.error_ ? " [ERROR]" : "",
                    widget.power_.clock_stop_ok_ ? " [ClkStopOK]" : "",
                    widget.power_.settings_reset_ ? " [Settings Reset]" : "");
    }

    if (widget.caps_.has_conn_list()) {
        printf(FMT(""), "ConnList");
        ihda_dump_conn_list(widget);
    }

    if (widget.caps_.proc_widget()) {
        printf(FMT("%s\n"), "Can Bypass Proc", widget.can_bypass_processing_ ? "yes" : "no");
        printf(FMT("%u\n"), "Proc Coefficients", widget.processing_coefficient_count_);
    }

    if (widget.caps_.type() == AudioWidgetCaps::Type::VOLUME_KNOB) {
        printf(FMT("%s\n"), "Vol Knob Type",  widget.vol_knob_is_delta_ ? "delta" : "absolute");
        printf(FMT("%u\n"), "Vol Knob Steps", widget.vol_knob_steps_);
    }

    printf("%s\n", pad);
}
#undef FMT

#define FMT(fmt) "%s%26s : " fmt, pad
static void ihda_dump_codec_fn_group(const CodecState& codec, uint32_t id) {
    ZX_DEBUG_ASSERT(codec.fn_groups_ && (id < codec.fn_group_count_) && codec.fn_groups_[id]);
    static const char* pad = "+--- ";
    const auto& fn_group = *codec.fn_groups_[id];

    printf("%sFunction Group %u/%u\n", pad, id + 1, codec.fn_group_count_);
    printf(FMT("%hu\n"), "Node ID", fn_group.nid_);
    printf(FMT("%s\n"),  "Type", ToString(fn_group.type_));

    if (fn_group.can_send_unsolicited_) {
        printf(FMT("%s [tag 0x%02x]\n"),  "Unsolicited Ctrl",
                   fn_group.unsol_resp_ctrl_.enabled() ? "enabled" : "disabled",
                   fn_group.unsol_resp_ctrl_.tag());
    }

    if (fn_group.type_ != FunctionGroupState::Type::AUDIO)
        return;

    const auto& afg = *reinterpret_cast<AudioFunctionGroupState*>(codec.fn_groups_[id].get());

    printf(FMT("%08x\n"), "Raw Caps", afg.caps_.raw_data_);
    printf(FMT("%s\n"),   "Beep Gen", afg.caps_.has_beep_gen() ? "yes" : "no");

    printf(FMT(""), "Input Path Delay");
    ihda_dump_delay(afg.caps_.path_input_delay());

    printf(FMT(""), "Output Path Delay");
    ihda_dump_delay(afg.caps_.path_output_delay());

    printf(FMT(""), "Default PCM Rates");
    DUMP_FLAGS(afg.default_pcm_size_rate_, PCM_RATE_FLAGS, "", "none");

    printf(FMT(""), "Default PCM Sizes");
    DUMP_FLAGS(afg.default_pcm_size_rate_, PCM_SIZE_FLAGS, " bits", "none");

    printf(FMT(""), "Default PCM Formats");
    DUMP_FLAGS(afg.default_pcm_formats_, PCM_FMT_FLAGS, "", "none");

    printf(FMT(""), "Default Input Amp Caps");
    Dump(afg.default_input_amp_caps_);

    printf(FMT(""), "Default Output Amp Caps");
    Dump(afg.default_output_amp_caps_);

    printf(FMT(""), "Sup. Pwr States");
    DUMP_FLAGS(afg.power_.supported_states_, POWER_STATE_FLAGS, "", "none");
    printf(FMT("Set %s(%u) Active %s(%u)%s%s%s\n"), "Cur Pwr State",
                PowerStateToString(afg.power_.set_), afg.power_.set_,
                PowerStateToString(afg.power_.active_), afg.power_.active_,
                afg.power_.error_ ? " [ERROR]" : "",
                afg.power_.clock_stop_ok_ ? " [ClkStopOK]" : "",
                afg.power_.settings_reset_ ? " [Settings Reset]" : "");

    printf(FMT("%u\n"), "GPIOs", afg.gpio_count_);
    printf(FMT("%u\n"), "GPIs",  afg.gpi_count_);
    printf(FMT("%u\n"), "GPOs",  afg.gpo_count_);
    printf(FMT("%s\n"), "GPIOs can wake", afg.gpio_can_wake_ ? "yes" : "no");
    printf(FMT("%s\n"), "GPIOs can send unsolicited",
           afg.gpio_can_send_unsolicited_ ? "yes" : "no");


    printf(FMT("BMID(%04hx) BSKU(%02x) AssyID(%02x) : Raw 0x%08x\n"), "Impl ID",
               fn_group.impl_id_.BoardMfrID(),
               fn_group.impl_id_.BoardSKU(),
               fn_group.impl_id_.AssemblyID(),
               fn_group.impl_id_.raw_data_);

    printf(FMT("%u\n"), "Widgets", afg.widget_count_);

    for (uint32_t i = 0; i < afg.widget_count_; ++i) {
        ZX_DEBUG_ASSERT(afg.widgets_[i]);
        ihda_dump_widget(*afg.widgets_[i], i + 1, afg.widget_count_);
    }
}
#undef FMT
}  // namespace

#define FMT(fmt) "%s%10s : " fmt, pad
void print_codec_state(const CodecState& codec) {
    static const char* pad = "+- ";

    printf(FMT("0x%04hx:0x%04hx\n"), "VID/DID", codec.vendor_id_, codec.device_id_);
    printf(FMT("%u.%u\n"), "Rev", codec.major_rev_, codec.minor_rev_);
    printf(FMT("%u.%u\n"), "Vendor Rev", codec.vendor_rev_id_, codec.vendor_stepping_id_);
    printf("%s%u function group%s\n",
           pad,  codec.fn_group_count_, codec.fn_group_count_ == 1 ? "" : "s");

    for (uint32_t i = 0; i < codec.fn_group_count_; ++i)
        ihda_dump_codec_fn_group(codec, i);
}
#undef FMT

}  // namespace audio
}  // namespace intel_hda
