// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/assert.h>
#include <zircon/types.h>
#include <fbl/unique_ptr.h>
#include <stddef.h>
#include <stdint.h>

#include <intel-hda/utils/codec-caps.h>
#include <intel-hda/utils/codec-commands.h>
#include <intel-hda/utils/codec-state.h>

namespace audio {
namespace intel_hda {

struct AudioWidgetState;
using  AudioWidgetStatePtr = fbl::unique_ptr<AudioWidgetState>;

struct FunctionGroupState;
struct AudioFunctionGroupState;
using  FunctionGroupStatePtr = fbl::unique_ptr<FunctionGroupState>;

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

    explicit AudioWidgetState(const AudioWidgetCaps& caps) : caps_(caps) { }

    const AudioWidgetCaps caps_;
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
    fbl::unique_ptr<ConnListEntry[]> conn_list_;
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
    AudioFunctionGroupState() : FunctionGroupState(Type::AUDIO) { }

    AudioFunctionGroupCaps caps_;
    uint32_t default_pcm_size_rate_; // Section 7.3.4.7 : Supported PCM sizes and rates
    uint32_t default_pcm_formats_;   // Section 7.3.4.8 : Supported PCM formats

    // Section 7.3.4.10 : Amplifier capabilities
    AmpCaps default_input_amp_caps_;
    AmpCaps default_output_amp_caps_;

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
    fbl::unique_ptr<AudioWidgetStatePtr[]> widgets_;
};

struct ModemFunctionGroupState : public FunctionGroupState {
    ModemFunctionGroupState() : FunctionGroupState(Type::MODEM) { }
};

struct VendorFunctionGroupState : public FunctionGroupState {
    explicit VendorFunctionGroupState(Type type)
        : FunctionGroupState(type) {
            ZX_DEBUG_ASSERT((type >= Type::VENDOR_START) &&
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
    fbl::unique_ptr<FunctionGroupStatePtr[]> fn_groups_;
};

}  // namespace audio
}  // namespace intel_hda
