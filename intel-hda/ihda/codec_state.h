// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/assert.h>
#include <magenta/types.h>
#include <mxtl/unique_ptr.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/audio/intel-hda/utils/codec-commands.h"

namespace audio {
namespace intel_hda {

/* Bitfield definitions for the PCM Size/Rate property.  See section 7.3.4.7 */
static constexpr uint32_t IHDA_PCM_SIZE_32BITS    = (1u << 20); // 32-bit PCM samples supported
static constexpr uint32_t IHDA_PCM_SIZE_24BITS    = (1u << 19); // 24-bit PCM samples supported
static constexpr uint32_t IHDA_PCM_SIZE_20BITS    = (1u << 18); // 20-bit PCM samples supported
static constexpr uint32_t IHDA_PCM_SIZE_16BITS    = (1u << 17); // 16-bit PCM samples supported
static constexpr uint32_t IHDA_PCM_SIZE_8BITS     = (1u << 16); // 8-bit PCM samples supported

static constexpr uint32_t IHDA_PCM_RATE_384000    = (1u << 11); // 384000 Hz
static constexpr uint32_t IHDA_PCM_RATE_192000    = (1u << 10); // 192000 Hz
static constexpr uint32_t IHDA_PCM_RATE_176400    = (1u <<  9); // 176400 Hz
static constexpr uint32_t IHDA_PCM_RATE_96000     = (1u <<  8); // 96000 Hz
static constexpr uint32_t IHDA_PCM_RATE_88200     = (1u <<  7); // 88200 Hz
static constexpr uint32_t IHDA_PCM_RATE_48000     = (1u <<  6); // 48000 Hz
static constexpr uint32_t IHDA_PCM_RATE_44100     = (1u <<  5); // 44100 Hz
static constexpr uint32_t IHDA_PCM_RATE_32000     = (1u <<  4); // 32000 Hz
static constexpr uint32_t IHDA_PCM_RATE_22050     = (1u <<  3); // 22050 Hz
static constexpr uint32_t IHDA_PCM_RATE_16000     = (1u <<  2); // 16000 Hz
static constexpr uint32_t IHDA_PCM_RATE_11025     = (1u <<  1); // 11025 Hz
static constexpr uint32_t IHDA_PCM_RATE_8000      = (1u <<  0); // 8000 Hz

/* Bitfield definitions for the PCM Formats property.  See section 7.3.4.8 */
static constexpr uint32_t IHDA_PCM_FORMAT_AC3     = (1u <<  2); // Dolby Digital AC-3 / ATSC A.52
static constexpr uint32_t IHDA_PCM_FORMAT_FLOAT32 = (1u <<  1); // 32-bit float
static constexpr uint32_t IHDA_PCM_FORMAT_PCM     = (1u <<  0); // PCM

/* Bitfield definitions for Supported Power States.  See section 7.3.4.12 */
static constexpr uint32_t IHDA_PWR_STATE_EPSS     = (1u << 31);
static constexpr uint32_t IHDA_PWR_STATE_CLKSTOP  = (1u << 30);
static constexpr uint32_t IHDA_PWR_STATE_S3D3COLD = (1u << 29);
static constexpr uint32_t IHDA_PWR_STATE_D3COLD   = (1u <<  4);
static constexpr uint32_t IHDA_PWR_STATE_D3       = (1u <<  3);
static constexpr uint32_t IHDA_PWR_STATE_D2       = (1u <<  2);
static constexpr uint32_t IHDA_PWR_STATE_D1       = (1u <<  1);
static constexpr uint32_t IHDA_PWR_STATE_D0       = (1u <<  0);

/* Defined pin capability flags.  See section 7.3.4.9 and Fig. 90 */
static constexpr uint32_t AW_PIN_CAPS_FLAG_CAN_IMPEDANCE_SENSE  = (1u << 0);
static constexpr uint32_t AW_PIN_CAPS_FLAG_TRIGGER_REQUIRED     = (1u << 1);
static constexpr uint32_t AW_PIN_CAPS_FLAG_CAN_PRESENCE_DETECT  = (1u << 2);
static constexpr uint32_t AW_PIN_CAPS_FLAG_CAN_DRIVE_HEADPHONES = (1u << 3);
static constexpr uint32_t AW_PIN_CAPS_FLAG_CAN_OUTPUT           = (1u << 4);
static constexpr uint32_t AW_PIN_CAPS_FLAG_CAN_INPUT            = (1u << 5);
static constexpr uint32_t AW_PIN_CAPS_FLAG_BALANCED_IO          = (1u << 6);
static constexpr uint32_t AW_PIN_CAPS_FLAG_HDMI                 = (1u << 7);
static constexpr uint32_t AW_PIN_CAPS_FLAG_VREF_HIZ             = (1u << 8);
static constexpr uint32_t AW_PIN_CAPS_FLAG_VREF_50_PERCENT      = (1u << 9);
static constexpr uint32_t AW_PIN_CAPS_FLAG_VREF_GROUND          = (1u << 10);
static constexpr uint32_t AW_PIN_CAPS_FLAG_VREF_80_PERCENT      = (1u << 12);
static constexpr uint32_t AW_PIN_CAPS_FLAG_VREF_100_PERCENT     = (1u << 13);
static constexpr uint32_t AW_PIN_CAPS_FLAG_CAN_EAPD             = (1u << 16);
static constexpr uint32_t AW_PIN_CAPS_FLAG_DISPLAY_PORT         = (1u << 24);
static constexpr uint32_t AW_PIN_CAPS_FLAG_HIGH_BIT_RATE        = (1u << 27);

struct AudioWidgetState;
using  AudioWidgetStatePtr = mxtl::unique_ptr<AudioWidgetState>;

struct FunctionGroupState;
struct AudioFunctionGroupState;
using  FunctionGroupStatePtr = mxtl::unique_ptr<FunctionGroupState>;

struct AmpCaps {
    // Bit packing documented in Section 7.3.4.10
    AmpCaps() { }
    explicit AmpCaps(uint32_t raw_data) : raw_data_(raw_data) { }
    uint32_t raw_data_ = 0;

    bool     can_mute()  const { return (raw_data_ & 0x80000000) != 0; }
    uint32_t step_size() const { return ((raw_data_ >> 16) & 0x7f) + 1; }
    uint32_t num_steps() const { return ((raw_data_ >>  8) & 0x7f) + 1; }
    uint32_t offset()    const { return ((raw_data_ >>  0) & 0x7f); }
};

struct PowerState {
    // Section 7.3.4.12 : Supported Power States
    uint32_t supported_states_;

    // Section 7.3.3.10 : Current power state
    uint8_t set_;
    uint8_t active_;
    bool    error_;
    bool    clock_stop_ok_;
    bool    settings_reset_;
};

// Section 7.3.3.14.  Present only in nodes (function groups and widgets) whose
// capabilities indicate the ability to send unsolicited responses.
struct UnsolicitedResponseState {
    bool    enabled() const { return (raw_data_ & 0x80) != 0; }
    uint8_t tag()     const { return static_cast<uint8_t>(raw_data_ & 0x3f); }
    uint8_t raw_data_;
};

struct AudioWidgetState {
    /* Defined audio widget types.  See Table 138 */
    enum class Type : uint8_t {
        OUTPUT      = 0x0,
        INPUT       = 0x1,
        MIXER       = 0x2,
        SELECTOR    = 0x3,
        PIN_COMPLEX = 0x4,
        POWER       = 0x5,
        VOLUME_KNOB = 0x6,
        BEEP_GEN    = 0x7,
        VENDOR      = 0xf,
    };

    struct Caps {
        static constexpr uint32_t FLAG_INPUT_AMP_PRESENT   = (1u << 1);
        static constexpr uint32_t FLAG_OUTPUT_AMP_PRESENT  = (1u << 2);
        static constexpr uint32_t FLAG_AMP_PARAM_OVERRIDE  = (1u << 3);
        static constexpr uint32_t FLAG_FORMAT_OVERRIDE     = (1u << 4);
        static constexpr uint32_t FLAG_STRIP_SUPPORTED     = (1u << 5);
        static constexpr uint32_t FLAG_PROC_WIDGET         = (1u << 6);
        static constexpr uint32_t FLAG_CAN_SEND_UNSOL      = (1u << 7);
        static constexpr uint32_t FLAG_HAS_CONN_LIST       = (1u << 8);
        static constexpr uint32_t FLAG_DIGITAL             = (1u << 9);
        static constexpr uint32_t FLAG_HAS_POWER_CTL       = (1u << 10);
        static constexpr uint32_t FLAG_CAN_LR_SWAP         = (1u << 11);
        static constexpr uint32_t FLAG_HAS_CONTENT_PROT    = (1u << 12);

        explicit Caps(uint32_t raw_data) : raw_data_(raw_data) { }

        /* Raw data format documented in section 7.3.4.6 */
        Type type()        const { return static_cast<Type>((raw_data_ >> 20) & 0xF); }
        uint8_t delay()    const { return static_cast<uint8_t>((raw_data_ >> 16) & 0xF); }
        uint8_t ch_count() const { return static_cast<uint8_t>((((raw_data_ >> 12) & 0xE) |
                                                                 (raw_data_ & 0x1)) + 1); }

        bool input_amp_present()  const { return (raw_data_ & FLAG_INPUT_AMP_PRESENT)  != 0; }
        bool output_amp_present() const { return (raw_data_ & FLAG_OUTPUT_AMP_PRESENT) != 0; }
        bool amp_param_override() const { return (raw_data_ & FLAG_AMP_PARAM_OVERRIDE) != 0; }
        bool format_override()    const { return (raw_data_ & FLAG_FORMAT_OVERRIDE)    != 0; }
        bool strip_supported()    const { return (raw_data_ & FLAG_STRIP_SUPPORTED)    != 0; }
        bool proc_widget()        const { return (raw_data_ & FLAG_PROC_WIDGET)        != 0; }
        bool can_send_unsol()     const { return (raw_data_ & FLAG_CAN_SEND_UNSOL)     != 0; }
        bool has_conn_list()      const { return (raw_data_ & FLAG_HAS_CONN_LIST)      != 0; }
        bool digital()            const { return (raw_data_ & FLAG_DIGITAL)            != 0; }
        bool has_power_ctl()      const { return (raw_data_ & FLAG_HAS_POWER_CTL)      != 0; }
        bool can_lr_swap()        const { return (raw_data_ & FLAG_CAN_LR_SWAP)        != 0; }
        bool has_content_prot()   const { return (raw_data_ & FLAG_HAS_CONTENT_PROT)   != 0; }

        const uint32_t raw_data_;
    };

    struct StreamFormat {
        // Stream format bitfields documented in section 3.7.1
        static constexpr uint16_t FLAG_NON_PCM = (1u << 15);

        uint32_t BASE() const { return (raw_data_ & (1u << 14)) ? 44100 : 48000; }
        uint32_t CHAN() const { return (raw_data_ & 0xF) + 1; }
        uint32_t DIV()  const { return ((raw_data_ >> 8) & 0x7) + 1; }
        uint32_t MULT() const {
            uint32_t bits = (raw_data_ >> 11) & 0x7;
            if (bits >= 4)
                return 0;
            return bits + 1;
        }
        uint32_t BITS() const {
            switch ((raw_data_ >> 4) & 0x7) {
            case 0: return 8u;
            case 1: return 16u;
            case 2: return 20u;
            case 3: return 24u;
            case 4: return 32u;
            default: return 0u;
            }
        }

        bool     is_pcm()        const { return (raw_data_ & FLAG_NON_PCM) == 0; }
        uint32_t sample_rate()   const { return (BASE() * MULT()) / DIV(); }
        uint32_t channels()      const { return CHAN(); }
        uint32_t bits_per_chan() const { return BITS(); }

        uint16_t raw_data_;
    };

    struct AmpState {
        uint8_t gain[2];
        bool    mute[2];
    };

    struct ConnListEntry {
        bool range_;
        uint16_t nid_;
        AmpState amp_state_;
    };

    // Section 7.3.3.15 and Table 92
    struct PinSenseState {
        bool     presence_detect() const { return (raw_data_ & 0x80000000u) != 0; }
        bool     eld_valid()       const { return (raw_data_ & 0x40000000u) != 0; }
        uint32_t impedance()       const { return (raw_data_ & 0x7fffffffu); }
        uint32_t raw_data_;
    };

    // Section 7.3.3.16
    struct EAPDState {
        bool btl()     const { return (raw_data_ & 0x1u) != 0; }
        bool eapd()    const { return (raw_data_ & 0x2u) != 0; }
        bool lr_swap() const { return (raw_data_ & 0x4u) != 0; }
        uint32_t raw_data_;
    };

    // Section 7.3.3.12.  Present only in pin complexes
    struct PinWidgetCtrlState {
        bool   hp_amp_enb()  const { return (raw_data_ & (1u << 7)) != 0; }
        bool   output_enb()  const { return (raw_data_ & (1u << 6)) != 0; }
        bool   input_enb()   const { return (raw_data_ & (1u << 5)) != 0; }
        VRefEn vref_enb()    const { return static_cast<VRefEn>(raw_data_ & 0xf); }
        EPT    ept()         const { return static_cast<EPT>(raw_data_ & 0x3); }

        uint8_t raw_data_;
    };

    // Section 7.3.3.31.  Present only in pin complexes
    struct ConfigDefaults {
        uint8_t port_connectivity() const { return static_cast<uint8_t>((raw_data_ >> 30) & 0x03); }
        uint8_t location()          const { return static_cast<uint8_t>((raw_data_ >> 24) & 0x3F); }
        uint8_t default_device()    const { return static_cast<uint8_t>((raw_data_ >> 20) & 0x0F); }
        uint8_t connection_type()   const { return static_cast<uint8_t>((raw_data_ >> 16) & 0x0F); }
        uint8_t color()             const { return static_cast<uint8_t>((raw_data_ >> 12) & 0x0F); }
        uint8_t misc()              const { return static_cast<uint8_t>((raw_data_ >>  8) & 0x0F); }
        uint8_t default_assoc()     const { return static_cast<uint8_t>((raw_data_ >>  4) & 0x0F); }
        uint8_t sequence()          const { return static_cast<uint8_t>((raw_data_ >>  0) & 0x0F); }
        bool jack_detect_override() const { return (misc() & 0x01) != 0; }

        uint32_t raw_data_;
    };

    explicit AudioWidgetState(const Caps& caps) : caps_(caps) { }

    const Caps caps_;
    const AudioFunctionGroupState* afg_ = nullptr;
    uint16_t nid_;

    // Note: to simplify life, the widget struct contains the union of all of
    // the different field which may be needed for any type of audio widget.
    // Not all of the fields will be meaningful depending on the widget type.
    uint32_t pcm_size_rate_; // Section 7.3.4.7 : Supported PCM sizes and rates
    uint32_t pcm_formats_;   // Section 7.3.4.8 : Supported PCM formats
    uint32_t pin_caps_ = 0;  // Section 7.3.4.9 : Pin Capabilities
    StreamFormat cur_format_;

    // Section 7.3.3.11 : Stream tag and channel routing for converters.
    uint8_t stream_tag_;
    uint8_t stream_chan_;

    // Section 7.3.4.10 : Amplifier capabilities
    AmpCaps input_amp_caps_;
    AmpCaps output_amp_caps_;

    // Section 7.3.3.7 : Amplifier Gain/Mute state
    AmpState input_amp_state_;
    AmpState output_amp_state_;

    // Sections 7.3.3.2, 7.3.3.3 & 7.3.4.11 : Connection List
    bool    long_form_conn_list_;
    uint8_t conn_list_len_;
    mxtl::unique_ptr<ConnListEntry[]> conn_list_;
    uint16_t connected_nid_;
    uint8_t  connected_nid_ndx_;

    // Sections 7.3.4.12 & 7.3.3.10.
    PowerState power_;

    // Section 7.3.4.13 : Processing Capabilities
    bool    can_bypass_processing_;
    uint8_t processing_coefficient_count_;

    // Section 7.3.4.15 : Volume Knob Capabilities
    bool    vol_knob_is_delta_;
    uint8_t vol_knob_steps_;

    // Section 7.3.3.31.  Present only in pin complexes
    ConfigDefaults cfg_defaults_;

    // Section 7.3.3.12.  Present only in pin complexes
    PinWidgetCtrlState pin_widget_ctrl_;

    // Section 7.3.3.14.
    UnsolicitedResponseState unsol_resp_ctrl_;

    // Section 7.3.3.15
    //
    // Only valid for pin complexes, only run if the pin complex supports
    // presence detect and the config defaults do not indicate a jack detect
    // override.
    PinSenseState pin_sense_;
    bool          pin_sense_valid_ = false;

    // Section 7.3.3.16 : External amp power down state
    EAPDState eapd_state_;
};

struct FunctionGroupState {
    virtual ~FunctionGroupState() { }

    enum class Type : uint8_t {
        AUDIO        = 0x01,
        MODEM        = 0x02,
        VENDOR_START = 0x80,
        VENDOR_END   = 0xFF,
    };

    // Section 7.3.3.30
    struct ImplementationID {
        uint32_t BoardImplID() const { return (raw_data_ >> 8)  & 0xFFFFFF; }
        uint16_t BoardMfrID()  const { return static_cast<uint16_t>(raw_data_ >> 16); }
        uint8_t  BoardSKU()    const { return static_cast<uint8_t>((raw_data_ >> 8) & 0xFF); }
        uint8_t  AssemblyID()  const { return static_cast<uint8_t>(raw_data_ & 0xFF); }
        uint32_t raw_data_;
    };

    const Type       type_;
    bool             can_send_unsolicited_;
    uint16_t         nid_;
    ImplementationID impl_id_;
    UnsolicitedResponseState unsol_resp_ctrl_;

protected:
    explicit FunctionGroupState(Type type)
        : type_(type) { }
};

struct AudioFunctionGroupState : public FunctionGroupState {
    // Section 7.3.4.5 : AFG Caps
    // Note: delays are expressed in audio frames.  If a path delay value is 0,
    // the delay should be computed by summing the delays of the widget chain
    // used to create either the input or output paths.
    struct Caps {
        static constexpr uint32_t FLAG_HAS_BEEP_GEN = (1u << 16);

        Caps() : raw_data_(0) { }
        explicit Caps(uint32_t raw_data) : raw_data_(raw_data) { }

        bool     has_beep_gen()      const { return (raw_data_ & FLAG_HAS_BEEP_GEN) != 0; }
        uint8_t  path_input_delay()  const { return static_cast<uint8_t>((raw_data_ >> 8) & 0xF); }
        uint8_t  path_output_delay() const { return static_cast<uint8_t>(raw_data_ & 0xF); }

        uint32_t raw_data_;
    };

    AudioFunctionGroupState() : FunctionGroupState(Type::AUDIO) { }

    Caps     caps_;
    uint32_t default_pcm_size_rate_; // Section 7.3.4.7 : Supported PCM sizes and rates
    uint32_t default_pcm_formats_;   // Section 7.3.4.8 : Supported PCM formats

    // Section 7.3.4.10 : Amplifier capabilities
    AmpCaps  default_input_amp_caps_;
    AmpCaps  default_output_amp_caps_;

    // Sections 7.3.4.12 & 7.3.3.10.
    PowerState power_;

    // Section 7.3.4.14 : GPIO Counts
    bool    gpio_can_wake_;
    bool    gpio_can_send_unsolicited_;
    uint8_t gpio_count_;
    uint8_t gpo_count_;
    uint8_t gpi_count_;

    uint8_t widget_count_ = 0;
    uint8_t widget_starting_id_ = 0;
    mxtl::unique_ptr<AudioWidgetStatePtr[]> widgets_;
};

struct ModemFunctionGroupState : public FunctionGroupState {
    ModemFunctionGroupState() : FunctionGroupState(Type::MODEM) { }
};

struct VendorFunctionGroupState : public FunctionGroupState {
    explicit VendorFunctionGroupState(Type type)
        : FunctionGroupState(type) {
            MX_DEBUG_ASSERT((type >= Type::VENDOR_START) &&
                            (type <= Type::VENDOR_START));
        }
};

struct CodecState {
    void reset() { fn_groups_.reset(); }

    uint16_t vendor_id_;
    uint16_t device_id_;

    uint8_t  major_rev_;
    uint8_t  minor_rev_;
    uint8_t  vendor_rev_id_;
    uint8_t  vendor_stepping_id_;

    uint8_t  fn_group_count_;
    uint8_t  fn_group_starting_id_;
    mxtl::unique_ptr<FunctionGroupStatePtr[]> fn_groups_;
};

}  // namespace audio
}  // namespace intel_hda
