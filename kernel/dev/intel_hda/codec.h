// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <sys/types.h>
#include <list.h>

#include "registers.h"

#define INTEL_HDA_MAX_CODECS (15u)

/* Bitfield definitions for the PCM Size/Rate property.  See section 7.3.4.7 */
#define IHDA_PCM_SIZE_32BITS    (1u << 20) // 32-bit PCM samples supported
#define IHDA_PCM_SIZE_24BITS    (1u << 19) // 24-bit PCM samples supported
#define IHDA_PCM_SIZE_20BITS    (1u << 18) // 20-bit PCM samples supported
#define IHDA_PCM_SIZE_16BITS    (1u << 17) // 16-bit PCM samples supported
#define IHDA_PCM_SIZE_8BITS     (1u << 16) // 8-bit PCM samples supported

#define IHDA_PCM_RATE_384000    (1u << 11) // 384000 Hz
#define IHDA_PCM_RATE_192000    (1u << 10) // 192000 Hz
#define IHDA_PCM_RATE_176400    (1u <<  9) // 176400 Hz
#define IHDA_PCM_RATE_96000     (1u <<  8) // 96000 Hz
#define IHDA_PCM_RATE_88200     (1u <<  7) // 88200 Hz
#define IHDA_PCM_RATE_48000     (1u <<  6) // 48000 Hz
#define IHDA_PCM_RATE_44100     (1u <<  5) // 44100 Hz
#define IHDA_PCM_RATE_32000     (1u <<  4) // 32000 Hz
#define IHDA_PCM_RATE_22050     (1u <<  3) // 22050 Hz
#define IHDA_PCM_RATE_16000     (1u <<  2) // 16000 Hz
#define IHDA_PCM_RATE_11025     (1u <<  1) // 11025 Hz
#define IHDA_PCM_RATE_8000      (1u <<  0) // 8000 Hz

/* Bitfield definitions for the PCM Formats property.  See section 7.3.4.8 */
#define IHDA_PCM_FORMAT_AC3     (1u <<  2) // Dolby Digital AC-3 / ATSC A.52
#define IHDA_PCM_FORMAT_FLOAT32 (1u <<  1) // 32-bit float
#define IHDA_PCM_FORMAT_PCM     (1u <<  0) // PCM; See PCM size rate for details

/* Bitfield definitions for Supported Power States.  See section 7.3.4.12 */
#define IHDA_PWR_STATE_EPSS     (1u << 31)
#define IHDA_PWR_STATE_CLKSTOP  (1u << 30)
#define IHDA_PWR_STATE_S3D3COLD (1u << 29)
#define IHDA_PWR_STATE_D3COLD   (1u <<  4)
#define IHDA_PWR_STATE_D3       (1u <<  3)
#define IHDA_PWR_STATE_D2       (1u <<  2)
#define IHDA_PWR_STATE_D1       (1u <<  1)
#define IHDA_PWR_STATE_D0       (1u <<  0)

/* Defined audio widget types.  See Table 138 */
#define AW_TYPE_OUTPUT          (0x0)
#define AW_TYPE_INPUT           (0x1)
#define AW_TYPE_MIXER           (0x2)
#define AW_TYPE_SELECTOR        (0x3)
#define AW_TYPE_PIN_COMPLEX     (0x4)
#define AW_TYPE_POWER           (0x5)
#define AW_TYPE_VOLUME_KNOB     (0x6)
#define AW_TYPE_BEEP_GEN        (0x7)
#define AW_TYPE_VENDOR          (0xf)

/* Defined audio widget capability flags.  See section 7.3.4.6 and Fig. 86 */
#define AW_CAPS_FLAG_INPUT_AMP_PRESENT   (1u << 1)
#define AW_CAPS_FLAG_OUTPUT_AMP_PRESENT  (1u << 2)
#define AW_CAPS_FLAG_AMP_PARAM_OVERRIDE  (1u << 3)
#define AW_CAPS_FLAG_FORMAT_OVERRIDE     (1u << 4)
#define AW_CAPS_FLAG_STRIP_SUPPORTED     (1u << 5)
#define AW_CAPS_FLAG_PROC_WIDGET         (1u << 6)
#define AW_CAPS_FLAG_CAN_SEND_UNSOL      (1u << 7)
#define AW_CAPS_FLAG_HAS_CONN_LIST       (1u << 8)
#define AW_CAPS_FLAG_DIGITAL             (1u << 9)
#define AW_CAPS_FLAG_HAS_POWER_CTL       (1u << 10)
#define AW_CAPS_FLAG_CAN_LR_SWAP         (1u << 11)
#define AW_CAPS_FLAG_HAS_CONTENT_PROT    (1u << 12)

#define AW_CAPS_INPUT_AMP_PRESENT(caps)  ((caps) & AW_CAPS_FLAG_INPUT_AMP_PRESENT)
#define AW_CAPS_OUTPUT_AMP_PRESENT(caps) ((caps) & AW_CAPS_FLAG_OUTPUT_AMP_PRESENT)
#define AW_CAPS_AMP_PARAM_OVERRIDE(caps) ((caps) & AW_CAPS_FLAG_AMP_PARAM_OVERRIDE)
#define AW_CAPS_FORMAT_OVERRIDE(caps)    ((caps) & AW_CAPS_FLAG_FORMAT_OVERRIDE)
#define AW_CAPS_STRIP_SUPPORTED(caps)    ((caps) & AW_CAPS_FLAG_STRIP_SUPPORTED)
#define AW_CAPS_PROC_WIDGET(caps)        ((caps) & AW_CAPS_FLAG_PROC_WIDGET)
#define AW_CAPS_CAN_SEND_UNSOL(caps)     ((caps) & AW_CAPS_FLAG_CAN_SEND_UNSOL)
#define AW_CAPS_HAS_CONN_LIST(caps)      ((caps) & AW_CAPS_FLAG_HAS_CONN_LIST)
#define AW_CAPS_DIGITAL(caps)            ((caps) & AW_CAPS_FLAG_DIGITAL)
#define AW_CAPS_HAS_POWER_CTL(caps)      ((caps) & AW_CAPS_FLAG_HAS_POWER_CTL)
#define AW_CAPS_CAN_LR_SWAP(caps)        ((caps) & AW_CAPS_FLAG_CAN_LR_SWAP)
#define AW_CAPS_HAS_CONTENT_PROT(caps)   ((caps) & AW_CAPS_FLAG_HAS_CONTENT_PROT)

/* Defined pin capability flags.  See section 7.3.4.9 and Fig. 90 */
#define AW_PIN_CAPS_FLAG_CAN_IMPEDANCE_SENSE  (1u << 0)
#define AW_PIN_CAPS_FLAG_TRIGGER_REQUIRED     (1u << 1)
#define AW_PIN_CAPS_FLAG_CAN_PRESENCE_DETECT  (1u << 2)
#define AW_PIN_CAPS_FLAG_CAN_DRIVE_HEADPHONES (1u << 3)
#define AW_PIN_CAPS_FLAG_CAN_OUTPUT           (1u << 4)
#define AW_PIN_CAPS_FLAG_CAN_INPUT            (1u << 5)
#define AW_PIN_CAPS_FLAG_BALANCED_IO          (1u << 6)
#define AW_PIN_CAPS_FLAG_HDMI                 (1u << 7)
#define AW_PIN_CAPS_FLAG_VREF_HIZ             (1u << 8)
#define AW_PIN_CAPS_FLAG_VREF_50_PERCENT      (1u << 9)
#define AW_PIN_CAPS_FLAG_VREF_GROUND          (1u << 10)
#define AW_PIN_CAPS_FLAG_VREF_80_PERCENT      (1u << 12)
#define AW_PIN_CAPS_FLAG_VREF_100_PERCENT     (1u << 13)
#define AW_PIN_CAPS_FLAG_CAN_EAPD             (1u << 16)
#define AW_PIN_CAPS_FLAG_DISPLAY_PORT         (1u << 24)
#define AW_PIN_CAPS_FLAG_HIGH_BIT_RATE        (1u << 27)

struct intel_hda_device;
struct intel_hda_codec;
struct intel_hda_codec_audio_fn_group;
struct intel_hda_widget_hdr;

typedef void (*intel_hda_codec_response_handler_fn)(struct intel_hda_codec* codec, uint32_t data);
typedef void (*intel_hda_codec_pending_work_handler_fn)(struct intel_hda_codec* codec);
typedef void (*intel_hda_codec_finished_command_list_handler_fn)(struct intel_hda_codec* codec);
typedef uint16_t (*intel_hda_codec_get_cmd_list_nid_fn)(struct intel_hda_codec* codec);

typedef struct intel_hda_command_list_entry {
    uint32_t verb;
    intel_hda_codec_response_handler_fn process_resp;
} intel_hda_command_list_entry_t;

typedef struct intel_hda_command_list_state {
    const intel_hda_command_list_entry_t* cmds;
    size_t cmd_count;
    size_t tx_ndx;
    size_t rx_ndx;
    intel_hda_codec_get_cmd_list_nid_fn get_nid;
    intel_hda_codec_finished_command_list_handler_fn finished_handler;
} intel_hda_command_list_state_t;

typedef struct intel_hda_codec_amp_caps {
    bool    can_mute;
    uint8_t step_size;  // amp gain step size in units of 0.25 dB
    uint8_t num_steps;  // Number of gain steps.  1 step means fixed, 0dB gain.
    uint8_t offset;     // The gain value which corresponds to 0dB
} intel_hda_codec_amp_caps_t;

typedef struct intel_hda_widget {
    struct intel_hda_codec_audio_fn_group* fn_group;
    uint16_t nid;

    // Note: to simplify life, the widget struct contains the union of all of
    // the different field which may be needed for any type of audio widget.
    // Not all of the fields will be meaningful depending on the widget type.
    uint32_t raw_caps;
    uint8_t  type;
    uint8_t  delay;
    uint8_t  ch_count;

    uint32_t pcm_size_rate; // Section 7.3.4.7 : Supported PCM sizes and rates
    uint32_t pcm_formats;   // Section 7.3.4.8 : Supported PCM formats
    uint32_t pin_caps;      // Section 7.3.4.9 : Pin Capabilities

    // Section 7.3.4.10 : Amplifier capabilities
    intel_hda_codec_amp_caps_t input_amp_caps;
    intel_hda_codec_amp_caps_t output_amp_caps;

    // Sections 7.3.3.3 & 7.3.4.11 : Connection List
    bool      long_form_conn_list;
    uint8_t   conn_list_len;
    uint16_t* conn_list;

    // Section 7.3.4.12 : Supported Power States
    uint32_t power_states;

    // Section 7.3.4.13 : Processing Capabilities
    bool    can_bypass_processing;
    uint8_t processing_coefficient_count;

    // Section 7.3.4.15 : Volume Knob Capabilities
    bool    vol_knob_is_delta;
    uint8_t vol_knob_steps;
} intel_hda_widget_t;

typedef struct intel_hda_codec_audio_fn_group {
    bool     can_send_unsolicited;
    uint8_t  fn_group_type;
    uint16_t nid;

    // Section 7.3.4.5 : AFG Caps
    // Note: delays are expressed in audio frames.  If a path delay value is 0,
    // the delay should be computed by summing the delays of the widget chain
    // used to create either the input or output paths.
    bool     has_beep_gen;
    uint8_t  path_input_delay;
    uint8_t  path_output_delay;

    uint32_t default_pcm_size_rate; // Section 7.3.4.7 : Supported PCM sizes and rates
    uint32_t default_pcm_formats;   // Section 7.3.4.8 : Supported PCM formats

    // Section 7.3.4.10 : Amplifier capabilities
    intel_hda_codec_amp_caps_t default_input_amp_caps;
    intel_hda_codec_amp_caps_t default_output_amp_caps;

    // Section 7.3.4.12 : Supported Power States
    uint32_t power_states;

    // Section 7.3.4.14 : GPIO Counts
    bool    gpio_can_wake;
    bool    gpio_can_send_unsolicited;
    uint8_t gpio_count;
    uint8_t gpo_count;
    uint8_t gpi_count;

    uint16_t widget_count;
    uint16_t widget_starting_id;
    intel_hda_widget_t* widgets;
} intel_hda_codec_audio_fn_group_t;

typedef struct intel_hda_codec {
    struct intel_hda_device* dev;
    uint8_t  codec_id;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t  major_rev;
    uint8_t  minor_rev;
    uint8_t  vendor_rev_id;
    uint8_t  vendor_stepping_id;

    uint16_t fn_group_count;
    uint16_t fn_group_starting_id;
    intel_hda_codec_audio_fn_group_t** fn_groups;

    // State machine callbacks and bookkeeping.  Used for enumerating codec
    // capabilities at startup.
    intel_hda_codec_response_handler_fn     solicited_response_handler;
    intel_hda_codec_response_handler_fn     unsolicited_response_handler;
    intel_hda_codec_pending_work_handler_fn pending_work_handler;
    intel_hda_command_list_state_t          cmd_list;
    uint16_t                                fn_group_iter;
    uint16_t                                widget_iter;
    uint8_t                                 conn_list_tx_iter;
    uint8_t                                 conn_list_rx_iter;
} intel_hda_codec_t;

/*
 * Create a codec for the specified device with the specified codec id.
 */
intel_hda_codec_t* intel_hda_create_codec(struct intel_hda_device* dev, uint8_t codec_id);

/*
 * Release all of the resources associated with a codec.
 */
void intel_hda_destroy_codec(intel_hda_codec_t* codec);

/*
 * Called once at the start of the codec service cycle.
 *
 * Observe the amount of space for new jobs in the CORB, and reset the
 * bookkeeping about the number of pending jobs and current write pointer
 * position.
 */
void intel_hda_codec_snapshot_corb(struct intel_hda_device* dev);

/*
 * Called once at the end of the codec service cycle.
 *
 * Update the CORB write pointer and begin transmitting any command requests
 * which were queued by codecs during the codec service cycle.
 */
void intel_hda_codec_commit_corb(struct intel_hda_device* dev);

/*
 * Called once at the start of the codec service cycle.
 *
 * Take a snapshot of any responses pending in the RIRB and stash in local
 * memory to minimize the chance of an undetectable ring buffer overflow..
 */
void intel_hda_codec_snapshot_rirb(struct intel_hda_device* dev);

/*
 * Called once in the middle of the codec service cycle.
 *
 * Go over the RIRB snapshot and dispatch any pending responses to the various
 * codecs.
 */
void intel_hda_codec_process_rirb(struct intel_hda_device* dev);

/*
 * Go over the list of codecs who may have pending work and give them a chance
 * to schedule communications on the link.
 */
void intel_hda_codec_process_pending_work(struct intel_hda_device* dev);
