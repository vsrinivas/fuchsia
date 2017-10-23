// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <endian.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <stddef.h>
#include <stdint.h>

namespace audio {
namespace intel_hda {

constexpr size_t   HDA_MAX_CODECS = 15u;
constexpr uint16_t HDA_MAX_NID    = 0x7F;

struct CodecVerb {
    explicit constexpr CodecVerb(uint32_t bits) : val(bits) { }
    const uint32_t val;

    bool SanityCheck() const { return ((val & ~0xFFFFFu) == 0); }
    bool is_set() const { return (val & 0x80000) == 0; }
    bool is_get() const { return (val & 0x80000) != 0; }
};

// See Section 7.1.2 and Figure 52 for details on command encoding.
//
// Note: Long form NID addressing is not supported, nor are the details of its
// encoding mechanism ("Indirect NID references") defined by the 1.0a
// specification)
struct CodecCommand {
    CodecCommand() { }

    CodecCommand(uint8_t codec_id, uint16_t nid, CodecVerb verb) {
        Pack(codec_id, nid, verb);
    }

    void Pack(uint8_t codec_id, uint16_t nid, CodecVerb verb) {
        data = verb.val
             | (static_cast<uint32_t>(nid) << 20)
             | (static_cast<uint32_t>(codec_id) << 28);
    }

    uint8_t   codec_id() const { return static_cast<uint8_t> ((data >> 28) & 0xFu); }
    uint16_t  nid()      const { return static_cast<uint16_t>((data >> 20) & 0x7Fu); }
    CodecVerb verb()     const { return CodecVerb(data & 0xFFFFFu); }

    static bool SanityCheck(uint8_t codec_id, uint16_t nid, CodecVerb verb) {
        // Sanity checks.
        //
        // ++ Codec IDs must be < 15; we don't support broadcast verbs (nor does
        //    the spec define any).
        // ++ Node IDs must be at most 7 bits, we do not support 15-bit NIDs
        //    right now.
        // ++ Verbs are limited to 20 bits.
        // ++ There must be at least one bit set in the verb/nid pair.
        //
        return ((codec_id < HDA_MAX_CODECS) &&
                (nid < HDA_MAX_NID)         &&
                (nid || verb.val)           &&
                verb.SanityCheck());
    }

    uint32_t data;
} __PACKED;

constexpr size_t HDA_CORB_MAX_ENTRIES = 256u;
constexpr size_t HDA_CORB_MAX_BYTES   = HDA_CORB_MAX_ENTRIES * sizeof(CodecCommand);

// See...
// Section 3.7 Figures 6 & 7 (Solicited and Unsolicited Data field packing)
// Section 4.4.2.1 Table 54 (DataEx field packing)
struct CodecResponse {
    CodecResponse() { }
    constexpr CodecResponse(uint32_t _data, uint32_t _data_ex) : data(_data), data_ex(_data_ex) { }

    uint32_t data;
    uint32_t data_ex;

    void OnReceived() {
        data    = le32toh(data);
        data_ex = le32toh(data_ex);
    }

    uint32_t caddr()        const { return (data_ex & 0xF); }
    bool     unsolicited()  const { return (data_ex & 0x10) != 0; }
    uint8_t  unsol_tag()    const { return static_cast<uint8_t>((data >> 26) & 0x3F); }
    uint8_t  unsol_subtag() const { return static_cast<uint8_t>((data >> 21) & 0x1F); }
    uint32_t unsol_data()   const { return (data & ((1u << 21) - 1)); }

} __PACKED;

constexpr size_t HDA_RIRB_MAX_ENTRIES = 256u;
constexpr size_t HDA_RIRB_MAX_BYTES   = HDA_RIRB_MAX_ENTRIES * sizeof(CodecResponse);

// VREF Enable definitions used in analog pin widget control
enum class VRefEn : uint8_t {
    HiZ  = 0u,    // Hi-Z
    P50  = 1u,    // 50%
    Gnd  = 2u,    // Grounded
    P80  = 4u,    // 80%
    P100 = 5u,    // 100%
};

// EncodedPacketType definitions used in digital pin widget control
enum class EPT : uint8_t {
    Native = 0u,    // Audio Sample or Audio Stream Packets (HDMI vs. DisplayPort)
    HBR    = 3u,    // High-Bit-Rate
};

// constexpr functions for making command verbs with either short or long payloads.
template <uint32_t ID>
static inline constexpr CodecVerb SP_VERB(uint8_t payload) {
    static_assert((ID & ~0xFFFu) == 0u, "Illegal ID for short payload codec verb!");
    return CodecVerb((ID << 8) | payload);
}

template <uint32_t ID>
static inline constexpr CodecVerb SP_GET_VERB() {
    static_assert((ID & ~0xFFu) == 0u, "Illegal ID for short payload codec GET verb!");
    return CodecVerb(SP_VERB<0xF00u | ID>(0u));
}

template <uint32_t ID>
static inline constexpr CodecVerb SP_SET_VERB(uint8_t payload) {
    static_assert((ID & ~0xFFu) == 0u, "Illegal ID for short payload codec SET verb!");
    return CodecVerb(SP_VERB<0x700u | ID>(payload));
}

template <uint32_t ID>
static inline constexpr CodecVerb LP_VERB(uint16_t payload) {
    static_assert((ID & ~0xFu) == 0u, "Illegal ID for long payload codec verb!");
    return CodecVerb((ID << 16) | payload);
}

template <uint32_t ID>
static inline constexpr CodecVerb LP_GET_VERB(uint16_t payload = 0u) {
    static_assert(ID < 0x7u, "Illegal ID for long payload codec GET verb!");
    return CodecVerb(LP_VERB<0x8 | ID>(payload));
}

template <uint32_t ID>
static inline constexpr CodecVerb LP_SET_VERB(uint16_t payload) {
    static_assert(ID < 0x7u, "Illegal ID for long payload codec SET verb!");
    return CodecVerb(LP_VERB<ID>(payload));
}

enum class CodecParam : uint8_t {
    VENDOR_ID                = 0x00,  // Section 7.3.4.1
    REVISION_ID              = 0x02,  // Section 7.3.4.2
    SUBORDINATE_NODE_COUNT   = 0x04,  // Section 7.3.4.3
    FUNCTION_GROUP_TYPE      = 0x05,  // Section 7.3.4.4
    AFG_CAPS                 = 0x08,  // Section 7.3.4.5
    AW_CAPS                  = 0x09,  // Section 7.3.4.6
    SUPPORTED_PCM_SIZE_RATE  = 0x0a,  // Section 7.3.4.7
    SUPPORTED_STREAM_FORMATS = 0x0b,  // Section 7.3.4.8
    PIN_CAPS                 = 0x0c,  // Section 7.3.4.9
    INPUT_AMP_CAPS           = 0x0d,  // Section 7.3.4.10
    OUTPUT_AMP_CAPS          = 0x12,  // Section 7.3.4.10
    CONNECTION_LIST_LEN      = 0x0e,  // Section 7.3.4.11
    SUPPORTED_PWR_STATES     = 0x0f,  // Section 7.3.4.12
    PROCESSING_CAPS          = 0x10,  // Section 7.3.4.13
    GPIO_COUNT               = 0x11,  // Section 7.3.4.14
    VOLUME_KNOB_CAPS         = 0x13,  // Section 7.3.4.15
};

static inline constexpr CodecParam AMP_CAPS(bool is_input) {
    return is_input ?  CodecParam::INPUT_AMP_CAPS : CodecParam::OUTPUT_AMP_CAPS;
}

// Sections 7.3.3.1, 7.3.4, 7.3.6, Table 140
static inline constexpr CodecVerb GET_PARAM(CodecParam param) {
    return SP_VERB<0xF00>(static_cast<uint8_t>(param));
}

// Section 7.3.3.3
static inline constexpr CodecVerb GET_CONNECTION_LIST_ENTRY(uint8_t offset) {
    return SP_VERB<0xF02>(offset);
}

// Section 7.3.3.7 and Figure 62
static inline constexpr CodecVerb GET_AMPLIFIER_GAIN_MUTE(bool input, bool right, uint8_t ndx = 0) {
    return LP_GET_VERB<0x03>(static_cast<uint16_t>((!input ? (1u << 15) : 0) |
                                                   (!right ? (1u << 13) : 0) |
                                                   (ndx & 0xF)));
}

static inline constexpr CodecVerb SET_AMPLIFIER_GAIN_MUTE(bool mute,
                                                          uint8_t gain_steps,
                                                          bool set_input,
                                                          bool set_output,
                                                          uint8_t ndx = 0,
                                                          bool set_left = true,
                                                          bool set_right = true) {
    return LP_SET_VERB<0x03>(static_cast<uint16_t>((set_output ? (1u << 15) : 0) |
                                                   (set_input  ? (1u << 14) : 0) |
                                                   (set_left   ? (1u << 13) : 0) |
                                                   (set_right  ? (1u << 12) : 0) |
                                                   (mute       ? (1u <<  7) : 0) |
                                                   (static_cast<uint16_t>(ndx & 0xF) << 8) |
                                                   (gain_steps & 0x7F)));
}

static inline constexpr CodecVerb SET_INPUT_AMPLIFIER_GAIN_MUTE(bool mute,
                                                                uint8_t gain_steps,
                                                                uint8_t ndx = 0,
                                                                bool set_left = true,
                                                                bool set_right = true) {
    return SET_AMPLIFIER_GAIN_MUTE(mute, gain_steps, true, false, ndx, set_left, set_right);
}

static inline constexpr CodecVerb SET_OUTPUT_AMPLIFIER_GAIN_MUTE(bool mute,
                                                                 uint8_t gain_steps,
                                                                 uint8_t ndx = 0,
                                                                 bool set_left = true,
                                                                 bool set_right = true) {
    return SET_AMPLIFIER_GAIN_MUTE(mute, gain_steps, false, true, ndx, set_left, set_right);
}


// Section 7.3.3.12
static inline constexpr CodecVerb SET_ANALOG_PIN_WIDGET_CTRL(bool enable_out,
                                                             bool enable_in,
                                                             bool enable_hp_amp,
                                                             VRefEn vref = VRefEn::HiZ) {
    return SP_SET_VERB<0x07>(static_cast<uint8_t>((enable_out    ? (1u << 6) : 0) |
                                                  (enable_in     ? (1u << 5) : 0) |
                                                  (enable_hp_amp ? (1u << 7) : 0) |
                                                  (static_cast<uint8_t>(vref) & 0x7)));
}

static inline constexpr CodecVerb SET_DIGITAL_PIN_WIDGET_CTRL(bool enable_out,
                                                              bool enable_in,
                                                              EPT ept = EPT::Native) {
    return SP_SET_VERB<0x07>(static_cast<uint8_t>((enable_out    ? (1u << 6) : 0) |
                                                  (enable_in     ? (1u << 5) : 0) |
                                                  (static_cast<uint8_t>(ept) & 0x3)));
}

// Section 7.3.3.11 and Table 85
static inline constexpr CodecVerb SET_CONVERTER_STREAM_CHAN(uint8_t stream_tag, uint8_t chan) {
    return SP_SET_VERB<0x06>(static_cast<uint8_t>(((stream_tag & 0xF) << 4) | (chan & 0xF)));
}

// Section 7.3.3.14 and Figure 68
static inline constexpr CodecVerb SET_UNSOLICITED_RESP_CTRL(bool enabled, uint8_t tag) {
    return SP_SET_VERB<0x08>(static_cast<uint8_t>((tag & 0x3F) | (enabled ? 0x80 : 0x00)));
}

// Section 7.3.3.15
static inline constexpr CodecVerb EXECUTE_PIN_SENSE(bool right_chan = false) {
    return SP_SET_VERB<0x09>(static_cast<uint8_t>(right_chan ? 0x01 : 0x00));
}

constexpr CodecVerb EXECUTE_FUNCTION_RESET = SP_SET_VERB<0xFF>(0); // Section 7.3.3.33

constexpr CodecVerb GET_CONNECTION_SELECT_CONTROL = SP_GET_VERB<0x01>(); // Section 7.3.3.2
constexpr CodecVerb GET_PROCESSING_STATE          = SP_GET_VERB<0x03>(); // Section 7.3.3.4
constexpr CodecVerb GET_COEFFICIENT_INDEX         = LP_GET_VERB<0x05>(); // Section 7.3.3.5
constexpr CodecVerb GET_PROCESSING_COEFFICIENT    = LP_GET_VERB<0x04>(); // Section 7.3.3.6
constexpr CodecVerb GET_CONVERTER_FORMAT          = LP_GET_VERB<0x02>(); // Section 7.3.3.8
constexpr CodecVerb GET_DIGITAL_CONV_CONTROL      = SP_GET_VERB<0x0D>(); // Section 7.3.3.9
constexpr CodecVerb GET_POWER_STATE               = SP_GET_VERB<0x05>(); // Section 7.3.3.10
constexpr CodecVerb GET_CONVERTER_STREAM_CHAN     = SP_GET_VERB<0x06>(); // Section 7.3.3.11
constexpr CodecVerb GET_INPUT_CONV_SDI_SELECT     = SP_GET_VERB<0x04>(); // Section 7.3.3.12
constexpr CodecVerb GET_PIN_WIDGET_CTRL           = SP_GET_VERB<0x07>(); // Section 7.3.3.13
constexpr CodecVerb GET_UNSOLICITED_RESP_CTRL     = SP_GET_VERB<0x08>(); // Section 7.3.3.14
constexpr CodecVerb GET_PIN_SENSE                 = SP_GET_VERB<0x09>(); // Section 7.3.3.15
constexpr CodecVerb GET_EAPD_BTL_ENABLE           = SP_GET_VERB<0x0C>(); // Section 7.3.3.16
constexpr CodecVerb GET_GPI_DATA                  = SP_GET_VERB<0x10>(); // Section 7.3.3.17
constexpr CodecVerb GET_GPI_WAKE_ENB_MASK         = SP_GET_VERB<0x11>(); // Section 7.3.3.18
constexpr CodecVerb GET_GPI_UNSOLICITED_ENB_MASK  = SP_GET_VERB<0x12>(); // Section 7.3.3.19
constexpr CodecVerb GET_GPI_STICKY_MASK           = SP_GET_VERB<0x13>(); // Section 7.3.3.20
constexpr CodecVerb GET_GPO_DATA                  = SP_GET_VERB<0x14>(); // Section 7.3.3.21
constexpr CodecVerb GET_GPIO_DATA                 = SP_GET_VERB<0x15>(); // Section 7.3.3.22
constexpr CodecVerb GET_GPIO_ENB_MASK             = SP_GET_VERB<0x16>(); // Section 7.3.3.23
constexpr CodecVerb GET_GPIO_DIR                  = SP_GET_VERB<0x17>(); // Section 7.3.3.24
constexpr CodecVerb GET_GPIO_WAKE_ENB_MASK        = SP_GET_VERB<0x18>(); // Section 7.3.3.25
constexpr CodecVerb GET_GPIO_UNSOLICITED_ENB_MASK = SP_GET_VERB<0x19>(); // Section 7.3.3.26
constexpr CodecVerb GET_GPIO_STICKY_MASK          = SP_GET_VERB<0x1a>(); // Section 7.3.3.27
constexpr CodecVerb GET_BEEP_GENERATION           = SP_GET_VERB<0x0a>(); // Section 7.3.3.28
constexpr CodecVerb GET_VOLUME_KNOB               = SP_GET_VERB<0x0f>(); // Section 7.3.3.29
constexpr CodecVerb GET_IMPLEMENTATION_ID         = SP_GET_VERB<0x20>(); // Section 7.3.3.30
constexpr CodecVerb GET_CONFIG_DEFAULT            = SP_GET_VERB<0x1c>(); // Section 7.3.3.31
constexpr CodecVerb GET_STRIPE_CONTROL            = SP_GET_VERB<0x24>(); // Section 7.3.3.32
constexpr CodecVerb GET_EDID_LIKE_DATA            = SP_GET_VERB<0x2F>(); // Section 7.3.3.34
constexpr CodecVerb GET_CONV_CHANNEL_COUNT        = SP_GET_VERB<0x2d>(); // Section 7.3.3.35
constexpr CodecVerb GET_DIP_SIZE                  = SP_GET_VERB<0x2e>(); // Section 7.3.3.36
constexpr CodecVerb GET_DIP_INDEX                 = SP_GET_VERB<0x30>(); // Section 7.3.3.37
constexpr CodecVerb GET_DIP_DATA                  = SP_GET_VERB<0x31>(); // Section 7.3.3.38
constexpr CodecVerb GET_DIP_XMIT_CTRL             = SP_GET_VERB<0x32>(); // Section 7.3.3.39
constexpr CodecVerb GET_CP_CONTROL                = SP_GET_VERB<0x33>(); // Section 7.3.3.40
constexpr CodecVerb GET_ASP_CHAN_MAPPING          = SP_GET_VERB<0x34>(); // Section 7.3.3.41

#define MAKE_SET_CMD(_name, _SLP, _val_type, _id) \
static inline constexpr CodecVerb _name(_val_type val) { return _SLP ## _SET_VERB<_id>(val); }

MAKE_SET_CMD(SET_CONNECTION_SELECT_CONTROL, SP, uint8_t,  0x01) // Section 7.3.3.2
MAKE_SET_CMD(SET_PROCESSING_STATE,          SP, uint8_t,  0x03) // Section 7.3.3.4
MAKE_SET_CMD(SET_COEFFICIENT_INDEX,         LP, uint16_t, 0x05) // Section 7.3.3.5
MAKE_SET_CMD(SET_PROCESSING_COEFFICIENT,    LP, uint16_t, 0x04) // Section 7.3.3.6
MAKE_SET_CMD(SET_AMPLIFIER_GAIN_MUTE,       LP, uint16_t, 0x03) // Section 7.3.3.7
MAKE_SET_CMD(SET_CONVERTER_FORMAT,          LP, uint16_t, 0x02) // Section 7.3.3.8
MAKE_SET_CMD(SET_DIGITAL_CONV_CONTROL_1,    SP, uint8_t,  0x0D) // Section 7.3.3.9
MAKE_SET_CMD(SET_DIGITAL_CONV_CONTROL_2,    SP, uint8_t,  0x0E) // Section 7.3.3.9
MAKE_SET_CMD(SET_DIGITAL_CONV_CONTROL_3,    SP, uint8_t,  0x3E) // Section 7.3.3.9
MAKE_SET_CMD(SET_DIGITAL_CONV_CONTROL_4,    SP, uint8_t,  0x3F) // Section 7.3.3.9
MAKE_SET_CMD(SET_POWER_STATE,               SP, uint8_t,  0x05) // Section 7.3.3.10
MAKE_SET_CMD(SET_INPUT_CONV_SDI_SELECT,     SP, uint8_t,  0x04) // Section 7.3.3.12
MAKE_SET_CMD(SET_EAPD_BTL_ENABLE,           SP, uint8_t,  0x0C) // Section 7.3.3.16
MAKE_SET_CMD(SET_GPI_DATA,                  SP, uint8_t,  0x10) // Section 7.3.3.17
MAKE_SET_CMD(SET_GPI_WAKE_ENB_MASK,         SP, uint8_t,  0x11) // Section 7.3.3.18
MAKE_SET_CMD(SET_GPI_UNSOLICITED_ENB_MASK,  SP, uint8_t,  0x12) // Section 7.3.3.19
MAKE_SET_CMD(SET_GPI_STICKY_MASK,           SP, uint8_t,  0x13) // Section 7.3.3.20
MAKE_SET_CMD(SET_GPO_DATA,                  SP, uint8_t,  0x14) // Section 7.3.3.21
MAKE_SET_CMD(SET_GPIO_DATA,                 SP, uint8_t,  0x15) // Section 7.3.3.22
MAKE_SET_CMD(SET_GPIO_ENB_MASK,             SP, uint8_t,  0x16) // Section 7.3.3.23
MAKE_SET_CMD(SET_GPIO_DIR,                  SP, uint8_t,  0x17) // Section 7.3.3.24
MAKE_SET_CMD(SET_GPIO_WAKE_ENB_MASK,        SP, uint8_t,  0x18) // Section 7.3.3.25
MAKE_SET_CMD(SET_GPIO_UNSOLICITED_ENB_MASK, SP, uint8_t,  0x19) // Section 7.3.3.26
MAKE_SET_CMD(SET_GPIO_STICKY_MASK,          SP, uint8_t,  0x1a) // Section 7.3.3.27
MAKE_SET_CMD(SET_BEEP_GENERATION,           SP, uint8_t,  0x0a) // Section 7.3.3.28
MAKE_SET_CMD(SET_VOLUME_KNOB,               SP, uint8_t,  0x0f) // Section 7.3.3.29
MAKE_SET_CMD(SET_IMPLEMENTATION_ID_1,       SP, uint8_t,  0x20) // Section 7.3.3.30
MAKE_SET_CMD(SET_IMPLEMENTATION_ID_2,       SP, uint8_t,  0x21) // Section 7.3.3.30
MAKE_SET_CMD(SET_IMPLEMENTATION_ID_3,       SP, uint8_t,  0x22) // Section 7.3.3.30
MAKE_SET_CMD(SET_IMPLEMENTATION_ID_4,       SP, uint8_t,  0x23) // Section 7.3.3.30
MAKE_SET_CMD(SET_CONFIG_DEFAULT_1,          SP, uint8_t,  0x1c) // Section 7.3.3.31
MAKE_SET_CMD(SET_CONFIG_DEFAULT_2,          SP, uint8_t,  0x1d) // Section 7.3.3.31
MAKE_SET_CMD(SET_CONFIG_DEFAULT_3,          SP, uint8_t,  0x1e) // Section 7.3.3.31
MAKE_SET_CMD(SET_CONFIG_DEFAULT_4,          SP, uint8_t,  0x1f) // Section 7.3.3.31
MAKE_SET_CMD(SET_STRIPE_CONTROL,            SP, uint8_t,  0x24) // Section 7.3.3.32
MAKE_SET_CMD(SET_CONV_CHANNEL_COUNT,        SP, uint8_t,  0x2d) // Section 7.3.3.35
MAKE_SET_CMD(SET_DIP_INDEX,                 SP, uint8_t,  0x30) // Section 7.3.3.37
MAKE_SET_CMD(SET_DIP_DATA,                  SP, uint8_t,  0x31) // Section 7.3.3.38
MAKE_SET_CMD(SET_DIP_XMIT_CTRL,             SP, uint8_t,  0x32) // Section 7.3.3.39
MAKE_SET_CMD(SET_CP_CONTROL,                SP, uint8_t,  0x33) // Section 7.3.3.40
MAKE_SET_CMD(SET_ASP_CHAN_MAPPING,          SP, uint8_t,  0x34) // Section 7.3.3.41

#undef MAKE_SET_CMD

// Constants used for power states.  See sections 7.3.3.10 and 7.3.4.12
constexpr uint8_t HDA_PS_D0     = 0;
constexpr uint8_t HDA_PS_D1     = 1;
constexpr uint8_t HDA_PS_D2     = 2;
constexpr uint8_t HDA_PS_D3HOT  = 3;
constexpr uint8_t HDA_PS_D3COLD = 4;

// Constants used for EAPD/BTL control.  See sections 7.3.3.16
constexpr uint8_t EAPD_BTL_BALANCED_OUT = 0x01u;
constexpr uint8_t EAPD_BTL_POWER_AMP    = 0x02u;
constexpr uint8_t EAPD_BTL_LR_SWAP      = 0x04u;

}  // namespace intel_hda
}  // namespace audio
