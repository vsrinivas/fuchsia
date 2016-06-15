// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include "codec.h"
#include "codec_commands.h"
#include "intel_hda.h"

#define LOCAL_TRACE 0

static void intel_hda_fetch_next_function_group(intel_hda_codec_t* codec);
static void intel_hda_fetch_next_widget(intel_hda_codec_t* codec);

/*
 * intel_hda_codec_send_cmd
 *
 * Queue a command to be sent to a codec via the CORB (Command Output Ring
 * Buffer).  Writes the command to the ring buffer, but does not advance the
 * write pointer to begin transmission.  That will happen at the end up the
 * update cycle when the driver calls intel_hda_codec_commit_corb.
 *
 * @note It is up to the caller to make sure that there is space in the CORB to
 * send the command by checking the corb_snapshot_space member of the codec's
 * device.
 *
 * @param codec A pointer to the codec which this command targets.
 * @param nid The Node ID of node in the codec which this command targets.
 * @param verb The encoded command to send.
 */
static inline void intel_hda_codec_send_cmd(intel_hda_codec_t* codec, uint16_t nid, uint32_t verb) {
    DEBUG_ASSERT(codec && codec->dev);
    intel_hda_device_t* dev = codec->dev;

    LTRACEF("Send Cmd: Codec ID %2u Node ID %3hu Verb 0x%05x\n",
            codec->codec_id, nid, verb);

    /*
     * Sanity check the parameters for the command before we compose it.
     *
     * Codec IDs must be < 15; we don't support broadcast verbs (nor does the spec define any).
     * Node IDs must be at most 7 bits, we do not support 15-bit NIDs right now.
     * Verbs are limited to 20 bits and must be non-0.  0 is an illegal verb.
     */
    DEBUG_ASSERT(codec->codec_id < 0xf);
    DEBUG_ASSERT(!(nid & ~0x7f));
    DEBUG_ASSERT(!(verb & ~0xfffff) && verb);

    /* Assert that we have space in the ring buffer.  It is the caller's job to
     * make sure that space exists before they call this method.  Also assert
     * that the write pointer is sane, and that we have mapped the CORB */
    DEBUG_ASSERT(dev->corb_snapshot_space);
    DEBUG_ASSERT(dev->corb_wr_ptr < dev->corb_entry_count);
    DEBUG_ASSERT(dev->corb);

    /* See Section 7.1.2 and Figure 52 for details on command encoding */
    uint32_t cmd = ((uint32_t)codec->codec_id << 28) | ((uint32_t)nid << 20) | verb;

    /* Write the command into the ring buffer and update the SW shadow of the
     * write pointer.  We will update the HW write pointer later on when we
     * commit the new CORB commands.
     *
     * Note:  Intel's ring buffers are a bit wonky.  See Section 4.4.1.4, but
     * the general idea is that to send a command, you write the command at WP
     * and then bump the WP.  Instead you write the command to (WP + 1) %
     * RING_SIZE, then update WP to be (WP + 1) % RING_SIZE.  IOW - The write
     * pointer always points to the last command written, not the place where
     * the next command will go.  This behavior holds in the RIRB direction as
     * well
     */
    dev->corb_wr_ptr = (dev->corb_wr_ptr + 1) & dev->corb_mask;
    dev->corb[dev->corb_wr_ptr].command = LE32(cmd);
    dev->corb_snapshot_space--;
}

/******************************************************************************
 *
 * A long list of functions used to parse parameters fetched during initial
 * codec capability enumeration.
 *
 ******************************************************************************/
static void intel_hda_parse_amp_caps(struct intel_hda_codec_amp_caps* caps, uint32_t data) {
    // Section 7.3.4.10 : Amplifier Capabilities
    caps->can_mute  = (data & 0x80000000) != 0;
    caps->step_size = ((data >> 16) & 0x7f) + 1;
    caps->num_steps = ((data >>  8) & 0x7f) + 1;
    caps->offset    = ((data >>  0) & 0x7f);
}

static void intel_hda_parse_widget_pcm_size_rate(struct intel_hda_codec* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);
    DEBUG_ASSERT(fn_group->widgets && (codec->widget_iter < fn_group->widget_count));

    intel_hda_widget_t* widget = fn_group->widgets + codec->widget_iter;
    DEBUG_ASSERT(widget->fn_group == fn_group);

    if (AW_CAPS_FORMAT_OVERRIDE(widget->raw_caps))
        widget->pcm_size_rate = data;
    else
        widget->pcm_size_rate = widget->fn_group->default_pcm_size_rate;
}

static void intel_hda_parse_widget_pcm_formats(struct intel_hda_codec* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);
    DEBUG_ASSERT(fn_group->widgets && (codec->widget_iter < fn_group->widget_count));

    intel_hda_widget_t* widget = fn_group->widgets + codec->widget_iter;
    DEBUG_ASSERT(widget->fn_group == fn_group);

    if (AW_CAPS_FORMAT_OVERRIDE(widget->raw_caps))
        widget->pcm_formats = data;
    else
        widget->pcm_formats = widget->fn_group->default_pcm_formats;
}

static void intel_hda_parse_widget_pin_caps(struct intel_hda_codec* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);
    DEBUG_ASSERT(fn_group->widgets && (codec->widget_iter < fn_group->widget_count));

    intel_hda_widget_t* widget = fn_group->widgets + codec->widget_iter;
    DEBUG_ASSERT(widget->fn_group == fn_group);

    widget->pin_caps = data;
}

static void intel_hda_parse_widget_input_amp_caps(struct intel_hda_codec* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);
    DEBUG_ASSERT(fn_group->widgets && (codec->widget_iter < fn_group->widget_count));

    intel_hda_widget_t* widget = fn_group->widgets + codec->widget_iter;
    DEBUG_ASSERT(widget->fn_group == fn_group);

    if (AW_CAPS_INPUT_AMP_PRESENT(widget->raw_caps)) {
        if (AW_CAPS_AMP_PARAM_OVERRIDE(widget->raw_caps))
            intel_hda_parse_amp_caps(&widget->input_amp_caps, data);
        else
            widget->input_amp_caps = widget->fn_group->default_input_amp_caps;
    } else {
        memset(&widget->input_amp_caps, 0, sizeof(widget->input_amp_caps));
    }
}

static void intel_hda_parse_widget_output_amp_caps(struct intel_hda_codec* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);
    DEBUG_ASSERT(fn_group->widgets && (codec->widget_iter < fn_group->widget_count));

    intel_hda_widget_t* widget = fn_group->widgets + codec->widget_iter;
    DEBUG_ASSERT(widget->fn_group == fn_group);

    if (AW_CAPS_OUTPUT_AMP_PRESENT(widget->raw_caps)) {
        if (AW_CAPS_AMP_PARAM_OVERRIDE(widget->raw_caps))
            intel_hda_parse_amp_caps(&widget->output_amp_caps, data);
        else
            widget->output_amp_caps = widget->fn_group->default_output_amp_caps;
    } else {
        memset(&widget->output_amp_caps, 0, sizeof(widget->output_amp_caps));
    }
}

static void intel_hda_parse_widget_connection_list_len(struct intel_hda_codec* codec,
                                                       uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);
    DEBUG_ASSERT(fn_group->widgets && (codec->widget_iter < fn_group->widget_count));

    intel_hda_widget_t* widget = fn_group->widgets + codec->widget_iter;
    DEBUG_ASSERT(widget->fn_group == fn_group);

    if (AW_CAPS_HAS_CONN_LIST(widget->raw_caps)) {
        widget->long_form_conn_list = ((data & 0x80) != 0);
        widget->conn_list_len = data & 0x7f;
    } else {
        widget->long_form_conn_list = false;
        widget->conn_list_len = 0;
    }
}

static void intel_hda_parse_widget_power_states(struct intel_hda_codec* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);
    DEBUG_ASSERT(fn_group->widgets && (codec->widget_iter < fn_group->widget_count));

    intel_hda_widget_t* widget = fn_group->widgets + codec->widget_iter;
    DEBUG_ASSERT(widget->fn_group == fn_group);

    /* TODO(johngro) : the spec is a bit unclear here.  In section 7.3.4.6 it
     * states that the power control bit in the audio widget capabilities
     * "indicates that the Power State control is supported on this widget".
     * It goes on to say that this can give the system finer grained power
     * control and that "the power states supported is reported by the Supported
     * Power States Parameter".  Finally, it says "In cases where this parameter
     * is not supported, the widget supports the same power states as the
     * function group".
     *
     * So; can it be the case that the widget supports the power state control
     * (section 7.3.3.10) but not the supported power state parameter (section
     * 7.3.4.12)?  If so, how would we detect this?  For now, my assumption is
     * that if the widget claims to support the control, but the parameter is
     * zero, that we are supposed to use the same value as was reported at the
     * function group level.
     */
    if (AW_CAPS_HAS_POWER_CTL(widget->raw_caps)) {
        widget->power_states = data
                             ? data
                             : widget->fn_group->power_states;
    }
}

static void intel_hda_parse_widget_processing_caps(struct intel_hda_codec* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);
    DEBUG_ASSERT(fn_group->widgets && (codec->widget_iter < fn_group->widget_count));

    intel_hda_widget_t* widget = fn_group->widgets + codec->widget_iter;
    DEBUG_ASSERT(widget->fn_group == fn_group);

    if (AW_CAPS_HAS_POWER_CTL(widget->raw_caps)) {
        widget->can_bypass_processing = (data & 0x1) != 0;
        widget->processing_coefficient_count = ((data >> 8) & 0xFF);
    } else {
        widget->can_bypass_processing = false;
        widget->processing_coefficient_count = 0;
    }
}

static void intel_hda_parse_widget_volume_knob_caps(struct intel_hda_codec* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);
    DEBUG_ASSERT(fn_group->widgets && (codec->widget_iter < fn_group->widget_count));

    intel_hda_widget_t* widget = fn_group->widgets + codec->widget_iter;
    DEBUG_ASSERT(widget->fn_group == fn_group);

    widget->vol_knob_is_delta = (data & 0x80) != 0;
    widget->vol_knob_steps    = (data & 0x7f);
}

static void intel_hda_parse_widget_type(struct intel_hda_codec* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);
    DEBUG_ASSERT(fn_group->widgets && (codec->widget_iter < fn_group->widget_count));

    /* Response format documented in section 7.3.4.6 */
    intel_hda_widget_t* widget = fn_group->widgets + codec->widget_iter;
    widget->nid      = codec->widget_iter + fn_group->widget_starting_id;
    widget->fn_group = fn_group;
    widget->raw_caps = data;
    widget->type     = (data >> 20) & 0xF;
    widget->delay    = (data >> 16) & 0xF;
    widget->ch_count = (((data >> 12) & 0xE) | (data & 0x1)) + 1;
}

static void intel_hda_parse_afg_caps(struct intel_hda_codec* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);

    // Section 7.3.4.5 : AFG Caps
    fn_group->has_beep_gen         = (data & 0x10000) != 0;
    fn_group->path_input_delay  = (data >> 12) & 0xF;
    fn_group->path_output_delay = data & 0xF;
}

static void intel_hda_parse_afg_pcm_size_rate(struct intel_hda_codec* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);

    // Section 7.3.4.7 : Supported PCM sizes and rates
    fn_group->default_pcm_size_rate = data;
}

static void intel_hda_parse_afg_pcm_formats(struct intel_hda_codec* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);

    // Section 7.3.4.8 : Supported PCM formats
    fn_group->default_pcm_formats = data;
}

static void intel_hda_parse_afg_output_amp_caps(struct intel_hda_codec* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);

    intel_hda_parse_amp_caps(&fn_group->default_output_amp_caps, data);
}

static void intel_hda_parse_afg_input_amp_caps(struct intel_hda_codec* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);

    intel_hda_parse_amp_caps(&fn_group->default_input_amp_caps, data);
}

static void intel_hda_parse_afg_power_states(struct intel_hda_codec* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);

    fn_group->power_states = data;
}

static void intel_hda_parse_afg_gpio_count(struct intel_hda_codec* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);

    // Section 7.3.4.14 : GPIO Counts
    fn_group->gpio_can_wake             = (data & 0x80000000) != 0;
    fn_group->gpio_can_send_unsolicited = (data & 0x40000000) != 0;
    fn_group->gpi_count                 = (data >> 16) & 0xFF;
    fn_group->gpo_count                 = (data >>  8) & 0xFF;
    fn_group->gpio_count                = (data >>  0) & 0xFF;
}

static void intel_hda_parse_afg_node_count(struct intel_hda_codec* codec, uint32_t data) {
    /* Response format documented in section 7.3.4.3 */
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);

    fn_group->widget_count       = data & 0xFF;
    fn_group->widget_starting_id = (data >> 16) & 0xFF;
}

static void intel_hda_parse_fn_group_type(struct intel_hda_codec* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));
    DEBUG_ASSERT(!codec->fn_groups[codec->fn_group_iter]);

    /* Response format documented in section 7.3.4.4 and Table 137 */
    uint8_t type  = data & 0xFF;
    bool    unsol = (data & 0x100) != 0;

    /* We only support Audio Function Groups at the moment.  If this is not an
     * AFG, don't bother to keep enumerating it. */
    if (type != 0x01) {
        LTRACEF("Ignoring unsupported function group type 0x%02x (Node ID %hu)\n",
                type, codec->fn_group_iter + codec->fn_group_starting_id);
        return;
    }

    intel_hda_codec_audio_fn_group_t* fn_group =
        (intel_hda_codec_audio_fn_group_t*)calloc(1, sizeof(intel_hda_codec_audio_fn_group_t));

    DEBUG_ASSERT(fn_group);
    fn_group->can_send_unsolicited = unsol;
    fn_group->fn_group_type        = type;
    fn_group->nid                  = codec->fn_group_iter + codec->fn_group_starting_id;
    codec->fn_groups[codec->fn_group_iter] = fn_group;
}

static void intel_hda_parse_vendor_id(struct intel_hda_codec* codec, uint32_t data) {
    /* Response format documented in section 7.3.4.1 */
    DEBUG_ASSERT(codec);
    codec->vendor_id = (data >> 16) & 0xFFFF;
    codec->device_id = data & 0xFFFF;
}

static void intel_hda_parse_revision_id(struct intel_hda_codec* codec, uint32_t data) {
    /* Response format documented in section 7.3.4.2 */
    DEBUG_ASSERT(codec);
    codec->major_rev          = (data >> 20) & 0xF;
    codec->minor_rev          = (data >> 16) & 0xF;
    codec->vendor_rev_id      = (data >>  8) & 0xFF;
    codec->vendor_stepping_id = data & 0xFF;
}

static void intel_hda_parse_fn_group_count(struct intel_hda_codec* codec, uint32_t data) {
    /* Response format documented in section 7.3.4.3 */
    DEBUG_ASSERT(codec);
    codec->fn_group_count = data & 0xFF;
    codec->fn_group_starting_id = (data >> 16) & 0xFF;
}

/******************************************************************************
 *
 * Tables of parameters and parameter parsers which may be supported for the
 * various objects we need to fetch information for during initial capability
 * enumeration.
 *
 ******************************************************************************/

/* Widget objects */
static const intel_hda_command_list_entry_t FETCH_AUDIO_INPUT_CAPS[] = {
    { CC_GET_PARAM(CC_PARAM_SUPPORTED_PCM_SIZE_RATE),  intel_hda_parse_widget_pcm_size_rate },
    { CC_GET_PARAM(CC_PARAM_SUPPORTED_STREAM_FORMATS), intel_hda_parse_widget_pcm_formats },
    { CC_GET_PARAM(CC_PARAM_INPUT_AMP_CAPS),           intel_hda_parse_widget_input_amp_caps },
    { CC_GET_PARAM(CC_PARAM_CONNECTION_LIST_LEN),      intel_hda_parse_widget_connection_list_len },
    { CC_GET_PARAM(CC_PARAM_SUPPORTED_PWR_STATES),     intel_hda_parse_widget_power_states },
    { CC_GET_PARAM(CC_PARAM_PROCESSING_CAPS),          intel_hda_parse_widget_processing_caps },
};

static const intel_hda_command_list_entry_t FETCH_AUDIO_OUTPUT_CAPS[] = {
    { CC_GET_PARAM(CC_PARAM_SUPPORTED_PCM_SIZE_RATE),  intel_hda_parse_widget_pcm_size_rate },
    { CC_GET_PARAM(CC_PARAM_SUPPORTED_STREAM_FORMATS), intel_hda_parse_widget_pcm_formats },
    { CC_GET_PARAM(CC_PARAM_OUTPUT_AMP_CAPS),          intel_hda_parse_widget_output_amp_caps },
    { CC_GET_PARAM(CC_PARAM_SUPPORTED_PWR_STATES),     intel_hda_parse_widget_power_states },
    { CC_GET_PARAM(CC_PARAM_PROCESSING_CAPS),          intel_hda_parse_widget_processing_caps },
};

static const intel_hda_command_list_entry_t FETCH_DIGITAL_PIN_COMPLEX_CAPS[] = {
    { CC_GET_PARAM(CC_PARAM_PIN_CAPS),             intel_hda_parse_widget_pin_caps },
    { CC_GET_PARAM(CC_PARAM_OUTPUT_AMP_CAPS),      intel_hda_parse_widget_output_amp_caps },
    { CC_GET_PARAM(CC_PARAM_CONNECTION_LIST_LEN),  intel_hda_parse_widget_connection_list_len },
    { CC_GET_PARAM(CC_PARAM_SUPPORTED_PWR_STATES), intel_hda_parse_widget_power_states },
    { CC_GET_PARAM(CC_PARAM_PROCESSING_CAPS),      intel_hda_parse_widget_processing_caps },
};

static const intel_hda_command_list_entry_t FETCH_NON_DIGITAL_PIN_COMPLEX_CAPS[] = {
    { CC_GET_PARAM(CC_PARAM_PIN_CAPS),             intel_hda_parse_widget_pin_caps },
    { CC_GET_PARAM(CC_PARAM_INPUT_AMP_CAPS),       intel_hda_parse_widget_input_amp_caps },
    { CC_GET_PARAM(CC_PARAM_OUTPUT_AMP_CAPS),      intel_hda_parse_widget_output_amp_caps },
    { CC_GET_PARAM(CC_PARAM_CONNECTION_LIST_LEN),  intel_hda_parse_widget_connection_list_len },
    { CC_GET_PARAM(CC_PARAM_SUPPORTED_PWR_STATES), intel_hda_parse_widget_power_states },
    { CC_GET_PARAM(CC_PARAM_PROCESSING_CAPS),      intel_hda_parse_widget_processing_caps },
};

static const intel_hda_command_list_entry_t FETCH_MIXER_CAPS[] = {
    { CC_GET_PARAM(CC_PARAM_INPUT_AMP_CAPS),       intel_hda_parse_widget_input_amp_caps },
    { CC_GET_PARAM(CC_PARAM_OUTPUT_AMP_CAPS),      intel_hda_parse_widget_output_amp_caps },
    { CC_GET_PARAM(CC_PARAM_CONNECTION_LIST_LEN),  intel_hda_parse_widget_connection_list_len },
    { CC_GET_PARAM(CC_PARAM_SUPPORTED_PWR_STATES), intel_hda_parse_widget_power_states },
};

static const intel_hda_command_list_entry_t FETCH_SELECTOR_CAPS[] = {
    { CC_GET_PARAM(CC_PARAM_INPUT_AMP_CAPS),       intel_hda_parse_widget_input_amp_caps },
    { CC_GET_PARAM(CC_PARAM_OUTPUT_AMP_CAPS),      intel_hda_parse_widget_output_amp_caps },
    { CC_GET_PARAM(CC_PARAM_CONNECTION_LIST_LEN),  intel_hda_parse_widget_connection_list_len },
    { CC_GET_PARAM(CC_PARAM_SUPPORTED_PWR_STATES), intel_hda_parse_widget_power_states },
    { CC_GET_PARAM(CC_PARAM_PROCESSING_CAPS),      intel_hda_parse_widget_processing_caps },
};

static const intel_hda_command_list_entry_t FETCH_POWER_CAPS[] = {
    { CC_GET_PARAM(CC_PARAM_CONNECTION_LIST_LEN),  intel_hda_parse_widget_connection_list_len },
    { CC_GET_PARAM(CC_PARAM_SUPPORTED_PWR_STATES), intel_hda_parse_widget_power_states },
};

static const intel_hda_command_list_entry_t FETCH_VOLUME_KNOB_CAPS[] = {
    { CC_GET_PARAM(CC_PARAM_CONNECTION_LIST_LEN),  intel_hda_parse_widget_connection_list_len },
    { CC_GET_PARAM(CC_PARAM_SUPPORTED_PWR_STATES), intel_hda_parse_widget_power_states },
    { CC_GET_PARAM(CC_PARAM_VOLUME_KNOB_CAPS),     intel_hda_parse_widget_volume_knob_caps },
};

static const intel_hda_command_list_entry_t ID_WIDGET_COMMANDS[] = {
    { CC_GET_PARAM(CC_PARAM_AW_CAPS), intel_hda_parse_widget_type },
};

/* Function Group objects.  Currently, we ignore Modem and Vendor specific
 * function groups. */
static const intel_hda_command_list_entry_t FETCH_AFG_PROPERTIES_COMMANDS[] = {
    { CC_GET_PARAM(CC_PARAM_AFG_CAPS),                 intel_hda_parse_afg_caps },
    { CC_GET_PARAM(CC_PARAM_SUPPORTED_PCM_SIZE_RATE),  intel_hda_parse_afg_pcm_size_rate },
    { CC_GET_PARAM(CC_PARAM_SUPPORTED_STREAM_FORMATS), intel_hda_parse_afg_pcm_formats },
    { CC_GET_PARAM(CC_PARAM_INPUT_AMP_CAPS),           intel_hda_parse_afg_input_amp_caps },
    { CC_GET_PARAM(CC_PARAM_OUTPUT_AMP_CAPS),          intel_hda_parse_afg_output_amp_caps },
    { CC_GET_PARAM(CC_PARAM_SUPPORTED_PWR_STATES),     intel_hda_parse_afg_power_states },
    { CC_GET_PARAM(CC_PARAM_GPIO_COUNT),               intel_hda_parse_afg_gpio_count },
    { CC_GET_PARAM(CC_PARAM_SUBORDINATE_NODE_COUNT),   intel_hda_parse_afg_node_count },
};

static const intel_hda_command_list_entry_t ID_FUNCTION_GROUP_COMMANDS[] = {
    { CC_GET_PARAM(CC_PARAM_FUNCTION_GROUP_TYPE), intel_hda_parse_fn_group_type },
};

/* The Codec object (root of the object tree) */
static const intel_hda_command_list_entry_t FETCH_CODEC_ROOT_COMMANDS[] = {
    { CC_GET_PARAM(CC_PARAM_VENDOR_ID),              intel_hda_parse_vendor_id },
    { CC_GET_PARAM(CC_PARAM_REVISION_ID),            intel_hda_parse_revision_id },
    { CC_GET_PARAM(CC_PARAM_SUBORDINATE_NODE_COUNT), intel_hda_parse_fn_group_count },
};

/******************************************************************************
 *
 * Methods used to set up and process a static command list.  Used to build the
 * state machine for fetching parameters during initial capability enumeration.
 * Throttles command transmits so that the CORB does not overflow.
 *
 ******************************************************************************/
static void intel_hda_rx_cmd_list(intel_hda_codec_t* codec, uint32_t data) {
    DEBUG_ASSERT(codec && codec->dev);

    intel_hda_command_list_state_t* s = &codec->cmd_list;
    DEBUG_ASSERT(s->rx_ndx <  s->tx_ndx);
    DEBUG_ASSERT(s->tx_ndx <= s->cmd_count);
    DEBUG_ASSERT(s->cmds);

    /* If we have a processing handler, process the response for the command we
     * sent earlier */
    const intel_hda_command_list_entry_t* cmd = s->cmds + s->rx_ndx;
    if (cmd->process_resp)
        cmd->process_resp(codec, data);

    /* If we are done processing, clear out the solicited response handler and
     * call any finished handler which may have been registered to advance the
     * state machine to the next stage of processing..
     */
    s->rx_ndx++;
    if (s->rx_ndx == s->cmd_count) {
        codec->solicited_response_handler = NULL;
        if (s->finished_handler)
            s->finished_handler(codec);
    }
}

static void intel_hda_tx_cmd_list(intel_hda_codec_t* codec) {
    DEBUG_ASSERT(codec && codec->dev);

    intel_hda_command_list_state_t* s = &codec->cmd_list;
    DEBUG_ASSERT(s->rx_ndx <= s->tx_ndx);
    DEBUG_ASSERT(s->tx_ndx <  s->cmd_count);
    DEBUG_ASSERT(s->cmds);

    /* As long as there is room in the CORB and we still have commands to send,
     * queue some commands. */
    intel_hda_device_t* dev = codec->dev;
    while (dev->corb_snapshot_space && (s->tx_ndx < s->cmd_count)) {
        DEBUG_ASSERT(s->get_nid);
        const intel_hda_command_list_entry_t* cmd = s->cmds + s->tx_ndx;
        uint16_t nid = s->get_nid(codec);

        intel_hda_codec_send_cmd(codec, nid, cmd->verb);
        s->tx_ndx++;
    }

    /* If we have queued all of our requests, we can remove our pending work
     * handler */
    if (s->tx_ndx == s->cmd_count)
        codec->pending_work_handler = NULL;
}

static void intel_hda_setup_cmd_list(
        intel_hda_codec_t* codec,
        const intel_hda_command_list_entry_t* cmds,
        size_t cmd_count,
        intel_hda_codec_get_cmd_list_nid_fn get_nid,
        intel_hda_codec_finished_command_list_handler_fn finished_handler) {
    DEBUG_ASSERT(codec && cmds && cmd_count);

    intel_hda_command_list_state_t* s = &codec->cmd_list;

    s->tx_ndx           = 0;
    s->rx_ndx           = 0;
    s->cmds             = cmds;
    s->cmd_count        = cmd_count;
    s->get_nid          = get_nid;
    s->finished_handler = finished_handler;

    codec->solicited_response_handler = intel_hda_rx_cmd_list;
    codec->pending_work_handler       = intel_hda_tx_cmd_list;
}

/******************************************************************************
 *
 * Methods used to fetch a widget's connection list.  Used during initial codec
 * capability enumeration.  This is always the last step of widget enumeration.
 * Once completed, the state machine will advance to fetching the capabilities
 * of the next widget (if any).
 *
 ******************************************************************************/
static void intel_hda_rx_fetch_conn_list(intel_hda_codec_t* codec, uint32_t data) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);
    DEBUG_ASSERT(fn_group->widgets && (codec->widget_iter < fn_group->widget_count));

    intel_hda_widget_t* widget = fn_group->widgets + codec->widget_iter;
    DEBUG_ASSERT(widget->fn_group == fn_group);
    DEBUG_ASSERT(codec->conn_list_rx_iter < widget->conn_list_len);

    /* If this is a long form connection list, unpack up to two 16 bit NIDs.
     * Otherwise, unpack up to 4 8-bit NIDs. */
    if (widget->long_form_conn_list) {
        for (uint i = 0;
             (i < 2) && (codec->conn_list_rx_iter < widget->conn_list_len);
             ++i, ++codec->conn_list_rx_iter) {
            widget->conn_list[codec->conn_list_rx_iter] = data & 0xFFFF;
            data >>= 16;
        }
    } else {
        for (uint i = 0;
             (i < 4) && (codec->conn_list_rx_iter < widget->conn_list_len);
             ++i, ++codec->conn_list_rx_iter) {
            widget->conn_list[codec->conn_list_rx_iter] = data & 0xFF;
            data >>= 8;
        }
    }

    /* Finished?  If so, move on to the next widget */
    if (codec->conn_list_rx_iter == widget->conn_list_len) {
        DEBUG_ASSERT(!codec->pending_work_handler);
        DEBUG_ASSERT(codec->conn_list_rx_iter >= widget->conn_list_len);
        codec->solicited_response_handler = NULL;
        intel_hda_fetch_next_widget(codec);
    }
}

static void intel_hda_tx_fetch_conn_list(intel_hda_codec_t* codec) {
    DEBUG_ASSERT(codec && codec->dev);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));
    struct intel_hda_device* dev = codec->dev;

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);
    DEBUG_ASSERT(fn_group->widgets && (codec->widget_iter < fn_group->widget_count));

    intel_hda_widget_t* widget = fn_group->widgets + codec->widget_iter;
    DEBUG_ASSERT(widget->fn_group == fn_group);
    DEBUG_ASSERT(codec->conn_list_tx_iter < widget->conn_list_len);

    while (dev->corb_snapshot_space && (codec->conn_list_tx_iter < widget->conn_list_len)) {
        intel_hda_codec_send_cmd(codec,
                                 widget->nid,
                                 CC_GET_CONNECTION_LIST_ENTRY(codec->conn_list_tx_iter));
        codec->conn_list_tx_iter += widget->long_form_conn_list ? 2 : 4;
    }

    /* If we are finished queueing requests, remove our pending work handler.
     * The solicited_response_handler will take care of finishing the fetch
     * operation. */
    if (codec->conn_list_tx_iter >= widget->conn_list_len)
        codec->pending_work_handler = NULL;
}

static void intel_hda_fetch_widget_connection_list(struct intel_hda_codec* codec) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);
    DEBUG_ASSERT(fn_group->widgets && (codec->widget_iter < fn_group->widget_count));

    intel_hda_widget_t* widget = fn_group->widgets + codec->widget_iter;
    DEBUG_ASSERT(widget->fn_group == fn_group);

    if (widget->conn_list_len) {
        /* Looks like this widget has a connection list.  Allocate storage for
         * it and set up the state machine to fetch the entries. */
        codec->conn_list_tx_iter          = 0;
        codec->conn_list_rx_iter          = 0;
        codec->solicited_response_handler = intel_hda_rx_fetch_conn_list;
        codec->pending_work_handler       = intel_hda_tx_fetch_conn_list;
        widget->conn_list                 = (uint16_t*)calloc(widget->conn_list_len,
                                                              sizeof(*widget->conn_list));
        DEBUG_ASSERT(widget->conn_list);
    } else {
        /* No connection list?  Just move on to the next widget. */
        intel_hda_fetch_next_widget(codec);
    }
}

/******************************************************************************
 *
 * GetNID and Finished functions for various stages of the enumeration state
 * machine.
 *
 * intel_hda_setup_cmd_list requires two function pointers in order to process a
 * command list for a particular object.
 *
 * 1) The GetNID function is responsible for supplying the Node ID of the object
 *    to target each time a command needs to be queued.
 * 2) The Finished function after the command list has been fully processed and
 *    is used to select and set-up the next stage of the state machine.
 *
 ******************************************************************************/

/* Widget GetNID and Finished functions */
static uint16_t intel_hda_fetch_widget_get_nid(intel_hda_codec_t* codec) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);

    return codec->widget_iter + fn_group->widget_starting_id;
}

static void intel_hda_id_widget_finished(intel_hda_codec_t* codec) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(!codec->solicited_response_handler);
    DEBUG_ASSERT(!codec->pending_work_handler);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);

    intel_hda_widget_t* widget = fn_group->widgets + codec->widget_iter;

    // Now that we know what type of widget we are dealing with, fetch the
    // parameters which are specific to it.  If we do not recognize the widget
    // type, or there are no widget specific parameters we actually care about,
    // just move on to the next widget.
    const intel_hda_command_list_entry_t* cmd_table = NULL;
    size_t cmd_table_len = 0;

    switch (widget->type) {
    case AW_TYPE_OUTPUT:
        cmd_table     = FETCH_AUDIO_OUTPUT_CAPS;
        cmd_table_len = countof(FETCH_AUDIO_OUTPUT_CAPS);
        break;

    case AW_TYPE_INPUT:
        cmd_table     = FETCH_AUDIO_INPUT_CAPS;
        cmd_table_len = countof(FETCH_AUDIO_INPUT_CAPS);
        break;

    case AW_TYPE_MIXER:
        cmd_table     = FETCH_MIXER_CAPS;
        cmd_table_len = countof(FETCH_MIXER_CAPS);
        break;

    case AW_TYPE_SELECTOR:
        cmd_table     = FETCH_SELECTOR_CAPS;
        cmd_table_len = countof(FETCH_SELECTOR_CAPS);
        break;

    case AW_TYPE_PIN_COMPLEX:
        if (AW_CAPS_DIGITAL(widget->raw_caps)) {
            cmd_table     = FETCH_DIGITAL_PIN_COMPLEX_CAPS;
            cmd_table_len = countof(FETCH_DIGITAL_PIN_COMPLEX_CAPS);
        } else {
            cmd_table     = FETCH_NON_DIGITAL_PIN_COMPLEX_CAPS;
            cmd_table_len = countof(FETCH_NON_DIGITAL_PIN_COMPLEX_CAPS);
        }
        break;

    case AW_TYPE_POWER:
        cmd_table     = FETCH_POWER_CAPS;
        cmd_table_len = countof(FETCH_POWER_CAPS);
        break;

    case AW_TYPE_VOLUME_KNOB:
        cmd_table     = FETCH_VOLUME_KNOB_CAPS;
        cmd_table_len = countof(FETCH_VOLUME_KNOB_CAPS);
        break;

    case AW_TYPE_BEEP_GEN:
    case AW_TYPE_VENDOR:
        intel_hda_fetch_next_widget(codec);
        return;

    default:
        LTRACEF("Unrecognized widget type 0x%02x at Node ID %hu in function group with Node ID "
                "%hu\n",
                widget->type, widget->nid, widget->fn_group->nid);
        intel_hda_fetch_next_widget(codec);
        return;
    }

    DEBUG_ASSERT(cmd_table && cmd_table_len);
    intel_hda_setup_cmd_list(codec,
                             cmd_table,
                             cmd_table_len,
                             intel_hda_fetch_widget_get_nid,
                             intel_hda_fetch_widget_connection_list);
}

/* Function Group GetNID and Finished functions */
static uint16_t intel_hda_fetch_function_group_get_nid(intel_hda_codec_t* codec) {
    DEBUG_ASSERT(codec);
    return codec->fn_group_iter + codec->fn_group_starting_id;
}

static void intel_hda_fetch_afg_properties_finished(struct intel_hda_codec* codec) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(!codec->solicited_response_handler);
    DEBUG_ASSERT(!codec->pending_work_handler);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);

    /* If this function group has widgets, allocate storage to hold their
     * pointers to their state.  */
    if (fn_group->widget_count) {
        fn_group->widgets = (intel_hda_widget_t*)calloc(
            fn_group->widget_count, sizeof(intel_hda_widget_t));
        DEBUG_ASSERT(fn_group->widgets);
    }

    /* Fetch next widget is always going to increment widget_iter.  Set its
     * value to -1 so the "next" widget it considers is widget 0. */
    codec->widget_iter = -1;
    intel_hda_fetch_next_widget(codec);
}

static void intel_hda_id_function_group_finished(intel_hda_codec_t* codec) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(!codec->solicited_response_handler);
    DEBUG_ASSERT(!codec->pending_work_handler);

    /* If this is a function group type we care about (IOW - an AFG), enumerate
     * it's properties.  Otherwise, just move on to the next function group (if
     * any) */
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));
    if (codec->fn_groups[codec->fn_group_iter]) {
        intel_hda_setup_cmd_list(codec,
                                 FETCH_AFG_PROPERTIES_COMMANDS,
                                 countof(FETCH_AFG_PROPERTIES_COMMANDS),
                                 intel_hda_fetch_function_group_get_nid,
                                 intel_hda_fetch_afg_properties_finished);
        return;
    }

    intel_hda_fetch_next_function_group(codec);
}

/* Codec Root GetNID and Finished functions */
static uint16_t intel_hda_fetch_codec_root_get_nid(intel_hda_codec_t* codec) {
    return 0;
}

static void intel_hda_fetch_codec_root_finished(intel_hda_codec_t* codec) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(!codec->solicited_response_handler);
    DEBUG_ASSERT(!codec->pending_work_handler);

    /* We are done fetching our root info.  If there are any function groups in
     * this codec (and I sure hope that there are, or this is the world's most
     * boring codec) start to enumerate their properties and widgets. */
    if (!codec->fn_group_count)
        return;

    codec->fn_groups = (intel_hda_codec_audio_fn_group_t**)calloc(
            codec->fn_group_count,
            sizeof(intel_hda_codec_audio_fn_group_t*));
    DEBUG_ASSERT(codec->fn_groups);

    /* Fetch next function group is always going to increment fn_group_iter.
     * Set its value to -1 so the "next" function group it considers is group 0. */
    codec->fn_group_iter = -1;
    intel_hda_fetch_next_function_group(codec);
}

/******************************************************************************
 *
 * "fetch next" functions, generally called from the Finished functions of the
 * various state machine stages in order to set up the state machine to move on
 * to the object once the current object is finished.
 *
 ******************************************************************************/
static void intel_hda_fetch_next_widget(intel_hda_codec_t* codec) {
    DEBUG_ASSERT(codec);
    DEBUG_ASSERT(codec->fn_groups && (codec->fn_group_iter < codec->fn_group_count));

    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[codec->fn_group_iter];
    DEBUG_ASSERT(fn_group);

    codec->widget_iter++;
    if (codec->widget_iter < fn_group->widget_count) {
        intel_hda_setup_cmd_list(codec,
                                 ID_WIDGET_COMMANDS,
                                 countof(ID_WIDGET_COMMANDS),
                                 intel_hda_fetch_widget_get_nid,
                                 intel_hda_id_widget_finished);
        return;
    }

    /* Looks like we are out of widgets to enumerate for this function group.
     * Move on to the next function group */
    intel_hda_fetch_next_function_group(codec);
}

static void intel_hda_fetch_next_function_group(intel_hda_codec_t* codec) {
    codec->fn_group_iter++;
    if (codec->fn_group_iter < codec->fn_group_count) {
        intel_hda_setup_cmd_list(codec,
                                 ID_FUNCTION_GROUP_COMMANDS,
                                 countof(ID_FUNCTION_GROUP_COMMANDS),
                                 intel_hda_fetch_function_group_get_nid,
                                 intel_hda_id_function_group_finished);
    } else {
        TRACEF("TODO(johngro): Codec configuration has been fetched.  Time to start setup!\n");
    }
}

/******************************************************************************
 *
 * Driver facing API
 *
 ******************************************************************************/
intel_hda_codec_t* intel_hda_create_codec(intel_hda_device_t* dev, uint8_t codec_id) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(codec_id < INTEL_HDA_MAX_CODECS);

    intel_hda_codec_t* codec = (intel_hda_codec_t*)calloc(1, sizeof(*codec));
    if (!codec) return NULL;

    codec->dev = dev;
    codec->codec_id = codec_id;

    /* Start the process of fetching the root info for our codec.  When we are
     * finished, we will know the total number of function groups in the codec
     * and can start to enumerate them. */
    intel_hda_setup_cmd_list(codec,
                             FETCH_CODEC_ROOT_COMMANDS,
                             countof(FETCH_CODEC_ROOT_COMMANDS),
                             intel_hda_fetch_codec_root_get_nid,
                             intel_hda_fetch_codec_root_finished);

    return codec;
}

void intel_hda_destroy_codec(intel_hda_codec_t* codec) {
    if (!codec)
        return;

    if (codec->fn_groups) {
        for (uint16_t i = 0; i < codec->fn_group_count; ++i) {
            intel_hda_codec_audio_fn_group_t* afg = codec->fn_groups[i];
            if (afg) {
                if (afg->widgets) {
                    for (uint16_t j = 0; j < afg->widget_count; ++j) {
                        intel_hda_widget_t* widget = afg->widgets + j;
                        if (widget->conn_list)
                            free(widget->conn_list);
                    }
                    free(afg->widgets);
                }
                free(afg);
            }
        }
        free(codec->fn_groups);
    }
    free(codec);
}

void intel_hda_codec_snapshot_corb(intel_hda_device_t* dev) {
    DEBUG_ASSERT(dev && dev->regs);
    DEBUG_ASSERT(dev->corb_entry_count && dev->corb_mask);

    hda_registers_t* r = dev->regs;
    DEBUG_ASSERT(dev->corb_wr_ptr == REG_RD(16, r, corbwp));
    uint corb_rd_ptr = REG_RD(16, r, corbrp) & dev->corb_mask;
    uint corb_used   = (dev->corb_entry_count + dev->corb_wr_ptr - corb_rd_ptr) & dev->corb_mask;

    /* The way the Intel HDA command ring buffers work, it is impossible to ever
     * be using more than N - 1 of the ring buffer entries.  Our available
     * space should be the ring buffer size, minus the amt currently used, minus 1 */
    DEBUG_ASSERT(dev->corb_entry_count   >  corb_used);
    DEBUG_ASSERT(dev->corb_max_in_flight >= corb_used);
    dev->corb_snapshot_space = dev->corb_max_in_flight - corb_used;

    LTRACEF("CORB has space for %u commands; WP is @%u\n",
            dev->corb_snapshot_space, dev->corb_wr_ptr);
}

void intel_hda_codec_commit_corb(intel_hda_device_t* dev) {
    DEBUG_ASSERT(dev && dev->regs);
    DEBUG_ASSERT(dev->corb_entry_count && dev->corb_mask);
    DEBUG_ASSERT(dev->corb_wr_ptr < dev->corb_entry_count);

    /* TODO(johngro) : Make sure to force a write back of the cache for the
     * dirty portions of the CORB before we update the write pointer if we are
     * running on an architecure where cache coherency is not automatically
     * managed for us via. snooping or by an explicit uncached or write-thru
     * policy set on our mapped pages in the MMU.  */

    LTRACEF("Update CORB WP; WP is @%u\n", dev->corb_wr_ptr);

    hda_registers_t* r = dev->regs;
    REG_WR(16, r, corbwp, dev->corb_wr_ptr);
}

void intel_hda_codec_snapshot_rirb(intel_hda_device_t* dev) {
    DEBUG_ASSERT(dev && dev->regs && dev->rirb);
    hda_registers_t* r = dev->regs;

    DEBUG_ASSERT(dev->rirb_entry_count && dev->rirb_mask);
    uint rirb_wr_ptr = REG_RD(16, r, rirbwp) & dev->rirb_mask;
    uint pending     = (dev->rirb_entry_count + rirb_wr_ptr - dev->rirb_rd_ptr) & dev->rirb_mask;

    /* Copy the current state of the RIRB into our snapshot memory.  Note: we
     * loop at most up to 2 times in order to deal with the case where the
     * active region of the ring buffer wraps around the end.
     *
     * TODO(johngro) : Make sure to invalidate cache for the memory region
     * occupied by the RIRB before we copy into our snapshot if we are running
     * on an architecure where cache coherency is not automatically managed for
     * us via. something like snooping, or by a un-cached policy set on our
     * mapped pages in the MMU. */
    dev->rirb_snapshot_cnt = 0;
    while (pending) {
         /* Intel HDA ring buffers are strange, see comments in
          * intel_hda_codec_send_cmd. */
        uint tmp_rd = (dev->rirb_rd_ptr + 1) & dev->rirb_mask;
        uint todo   = MIN(pending, (dev->rirb_entry_count - tmp_rd));

        memcpy(dev->rirb_snapshot + dev->rirb_snapshot_cnt,
               dev->rirb + tmp_rd,
               sizeof(dev->rirb_snapshot[0]) * todo);

        dev->rirb_rd_ptr = (dev->rirb_rd_ptr + todo) & dev->rirb_mask;
        dev->rirb_snapshot_cnt += todo;
        pending -= todo;
    }
}

void intel_hda_codec_process_rirb(intel_hda_device_t* dev) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->rirb_snapshot_cnt < HDA_RIRB_MAX_ENTRIES);
    DEBUG_ASSERT(dev->rirb_snapshot_cnt < dev->rirb_entry_count);

    for (uint i = 0; i < dev->rirb_snapshot_cnt; ++i) {
        hda_rirb_entry_t* resp = dev->rirb_snapshot + i;

        /* Fixup endianness */
        resp->data    = LE32(resp->data);
        resp->data_ex = LE32(resp->data_ex);

        /* Figure out the codec this came from and whether or not the response
         * was solicited. */
        uint caddr = HDA_RIRB_CADDR(*resp);
        bool unsolicited = HDA_RIRB_UNSOL(*resp);

        /* Sanity checks */
        if (caddr >= countof(dev->codecs)) {
            TRACEF("Received %ssolicited response with illegal codec address (%u) "
                   "[0x%08x, 0x%08x]\n",
                   unsolicited ? "un" : "", caddr, resp->data, resp->data_ex);
            continue;
        }

        intel_hda_codec_t* codec = dev->codecs[caddr];
        if (!codec) {
            TRACEF("Received %ssolicited response for non-existent codec address (%u) "
                   "[0x%08x, 0x%08x]\n",
                   unsolicited ? "un" : "", caddr, resp->data, resp->data_ex);
            continue;
        }

        intel_hda_codec_response_handler_fn handler = unsolicited
                                                    ? codec->unsolicited_response_handler
                                                    : codec->solicited_response_handler;
        if (!handler) {
            TRACEF("Received %ssolicited response, but codec with address %u has no handler "
                   "[0x%08x, 0x%08x]\n",
                   unsolicited ? "un" : "", caddr, resp->data, resp->data_ex);
            continue;
        }

        /* Dispatch the response */
        LTRACEF("RX Cmd: Codec ID %2u Data 0x%08x\n", caddr, resp->data);
        handler(codec, resp->data);
    }

    dev->rirb_snapshot_cnt = 0;
}

void intel_hda_codec_process_pending_work(intel_hda_device_t* dev) {
    for (size_t i = 0; i < countof(dev->codecs); ++i) {
        intel_hda_codec_t* codec = dev->codecs[i];
        if (codec && codec->pending_work_handler)
            codec->pending_work_handler(codec);
    }
}
