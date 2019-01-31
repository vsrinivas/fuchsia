// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/audio.h>
#include <stdint.h>

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

// Section 7.3.4.5 : AFG Caps
// Note: delays are expressed in audio frames.  If a path delay value is 0,
// the delay should be computed by summing the delays of the widget chain
// used to create either the input or output paths.
struct AudioFunctionGroupCaps {
    static constexpr uint32_t FLAG_HAS_BEEP_GEN = (1u << 16);

    AudioFunctionGroupCaps() { }
    explicit AudioFunctionGroupCaps(uint32_t raw_data) : raw_data_(raw_data) { }

    bool     has_beep_gen()      const { return (raw_data_ & FLAG_HAS_BEEP_GEN) != 0; }
    uint8_t  path_input_delay()  const { return static_cast<uint8_t>((raw_data_ >> 8) & 0xF); }
    uint8_t  path_output_delay() const { return static_cast<uint8_t>(raw_data_ & 0xF); }

    uint32_t raw_data_ = 0;
};

struct AudioWidgetCaps {
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

    static constexpr uint32_t FLAG_INPUT_AMP_PRESENT   = (1u << 1);
    static constexpr uint32_t FLAG_OUTPUT_AMP_PRESENT  = (1u << 2);
    static constexpr uint32_t FLAG_AMP_PARAM_OVERRIDE  = (1u << 3);
    static constexpr uint32_t FLAG_FORMAT_OVERRIDE     = (1u << 4);
    static constexpr uint32_t FLAG_STRIPE_SUPPORTED    = (1u << 5);
    static constexpr uint32_t FLAG_PROC_WIDGET         = (1u << 6);
    static constexpr uint32_t FLAG_CAN_SEND_UNSOL      = (1u << 7);
    static constexpr uint32_t FLAG_HAS_CONN_LIST       = (1u << 8);
    static constexpr uint32_t FLAG_DIGITAL             = (1u << 9);
    static constexpr uint32_t FLAG_HAS_POWER_CTL       = (1u << 10);
    static constexpr uint32_t FLAG_CAN_LR_SWAP         = (1u << 11);
    static constexpr uint32_t FLAG_HAS_CONTENT_PROT    = (1u << 12);

    AudioWidgetCaps() { }
    explicit AudioWidgetCaps(uint32_t raw_data) : raw_data_(raw_data) { }

    /* Raw data format documented in section 7.3.4.6 */
    Type type()        const { return static_cast<Type>((raw_data_ >> 20) & 0xF); }
    uint8_t delay()    const { return static_cast<uint8_t>((raw_data_ >> 16) & 0xF); }
    uint8_t ch_count() const { return static_cast<uint8_t>((((raw_data_ >> 12) & 0xE) |
                                                             (raw_data_ & 0x1)) + 1); }

    bool input_amp_present()  const { return (raw_data_ & FLAG_INPUT_AMP_PRESENT)  != 0; }
    bool output_amp_present() const { return (raw_data_ & FLAG_OUTPUT_AMP_PRESENT) != 0; }
    bool amp_param_override() const { return (raw_data_ & FLAG_AMP_PARAM_OVERRIDE) != 0; }
    bool format_override()    const { return (raw_data_ & FLAG_FORMAT_OVERRIDE)    != 0; }
    bool stripe_supported()   const { return (raw_data_ & FLAG_STRIPE_SUPPORTED)   != 0; }
    bool proc_widget()        const { return (raw_data_ & FLAG_PROC_WIDGET)        != 0; }
    bool can_send_unsol()     const { return (raw_data_ & FLAG_CAN_SEND_UNSOL)     != 0; }
    bool has_conn_list()      const { return (raw_data_ & FLAG_HAS_CONN_LIST)      != 0; }
    bool digital()            const { return (raw_data_ & FLAG_DIGITAL)            != 0; }
    bool has_power_ctl()      const { return (raw_data_ & FLAG_HAS_POWER_CTL)      != 0; }
    bool can_lr_swap()        const { return (raw_data_ & FLAG_CAN_LR_SWAP)        != 0; }
    bool has_content_prot()   const { return (raw_data_ & FLAG_HAS_CONTENT_PROT)   != 0; }

    uint32_t raw_data_ = 0;
};

struct SampleCaps {
    // Bit packing documented in Sections 7.3.4.7 (size/rate) and 7.3.4.8 (format)
    SampleCaps() { }
    SampleCaps(uint32_t size_rate, uint32_t formats)
        : pcm_size_rate_(size_rate),
          pcm_formats_(formats) { }

    bool SupportsRate(uint32_t rate) const;
    bool SupportsFormat(audio_sample_format_t sample_format) const;

    uint32_t pcm_size_rate_ = 0;
    uint32_t pcm_formats_ = 0;
};

struct AmpCaps {
    // Bit packing documented in Section 7.3.4.10
    AmpCaps() { }
    explicit AmpCaps(uint32_t raw_data) : raw_data_(raw_data) { }
    uint32_t raw_data_ = 0;

    bool     can_mute()     const { return (raw_data_ & 0x80000000) != 0; }
    uint32_t step_size()    const { return ((raw_data_ >> 16) & 0x7f) + 1; }
    uint32_t num_steps()    const { return ((raw_data_ >>  8) & 0x7f) + 1; }
    uint32_t offset()       const { return ((raw_data_ >>  0) & 0x7f); }

    float    step_size_db() const { return 0.25f * static_cast<float>(step_size()); }
    float    min_gain_db()  const { return -step_size_db() * static_cast<float>(offset()); }
    float    max_gain_db()  const { return min_gain_db() + (step_size_db() *
                                    static_cast<float>((num_steps() - 1))); }
};

struct PinCaps {
    // Bit packing documented in Section 7.3.4.10
    PinCaps() { }
    explicit PinCaps(uint32_t raw_data) : raw_data_(raw_data) { }

    bool can_imp_sense()        const { return raw_data_ & AW_PIN_CAPS_FLAG_CAN_IMPEDANCE_SENSE; }
    bool trig_required()        const { return raw_data_ & AW_PIN_CAPS_FLAG_TRIGGER_REQUIRED; }
    bool can_pres_detect()      const { return raw_data_ & AW_PIN_CAPS_FLAG_CAN_PRESENCE_DETECT; }
    bool can_drive_headphones() const { return raw_data_ & AW_PIN_CAPS_FLAG_CAN_DRIVE_HEADPHONES; }
    bool can_output()           const { return raw_data_ & AW_PIN_CAPS_FLAG_CAN_OUTPUT; }
    bool can_input()            const { return raw_data_ & AW_PIN_CAPS_FLAG_CAN_INPUT; }
    bool balanced_io()          const { return raw_data_ & AW_PIN_CAPS_FLAG_BALANCED_IO; }
    bool is_hdmi()              const { return raw_data_ & AW_PIN_CAPS_FLAG_HDMI; }
    bool vref_hi_z()            const { return raw_data_ & AW_PIN_CAPS_FLAG_VREF_HIZ; }
    bool vref_50()              const { return raw_data_ & AW_PIN_CAPS_FLAG_VREF_50_PERCENT; }
    bool vref_gnd()             const { return raw_data_ & AW_PIN_CAPS_FLAG_VREF_GROUND; }
    bool vref_80()              const { return raw_data_ & AW_PIN_CAPS_FLAG_VREF_80_PERCENT; }
    bool vref_100()             const { return raw_data_ & AW_PIN_CAPS_FLAG_VREF_100_PERCENT; }
    bool has_eapd()             const { return raw_data_ & AW_PIN_CAPS_FLAG_CAN_EAPD; }
    bool is_display_port()      const { return raw_data_ & AW_PIN_CAPS_FLAG_DISPLAY_PORT; }
    bool hdmi_hbr()             const { return raw_data_ & AW_PIN_CAPS_FLAG_HIGH_BIT_RATE; }

    uint32_t raw_data_ = 0;
};


struct ConfigDefaults {
    // Bit packing documented in Section 7.3.3.31.  Present only in pin complexes
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

}  // namespace audio
}  // namespace intel_hda
