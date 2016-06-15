// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

// Helper macros for making command verbs with either short or long payloads.
#define CC_SP_VERB(id, payload) ((((uint32_t)(id) & 0xFFF) << 8)  | ((uint8_t)(payload)))
#define CC_LP_VERB(id, payload) ((((uint32_t)(id) & 0xF)   << 16) | ((uint16_t)(payload)))

#define CC_SP_GET_VERB(id)          CC_SP_VERB(0xF00 | id, 0)
#define CC_SP_SET_VERB(id, payload) CC_SP_VERB(0x700 | id, payload)
#define CC_LP_GET_VERB(id)          CC_LP_VERB(0x8 + id, 0)
#define CC_LP_SET_VERB(id, payload) CC_LP_VERB(0x0 + id, payload)

// Sections 7.3.3.1, 7.3.4, 7.3.6, Table 140
#define CC_GET_PARAM(param_id)                CC_SP_VERB(0xF00, param_id)
#define CC_PARAM_VENDOR_ID                    (0x00) // Section 7.3.4.1
#define CC_PARAM_REVISION_ID                  (0x02) // Section 7.3.4.2
#define CC_PARAM_SUBORDINATE_NODE_COUNT       (0x04) // Section 7.3.4.3
#define CC_PARAM_FUNCTION_GROUP_TYPE          (0x05) // Section 7.3.4.4
#define CC_PARAM_AFG_CAPS                     (0x08) // Section 7.3.4.5
#define CC_PARAM_AW_CAPS                      (0x09) // Section 7.3.4.6
#define CC_PARAM_SUPPORTED_PCM_SIZE_RATE      (0x0a) // Section 7.3.4.7
#define CC_PARAM_SUPPORTED_STREAM_FORMATS     (0x0b) // Section 7.3.4.8
#define CC_PARAM_PIN_CAPS                     (0x0c) // Section 7.3.4.9
#define CC_PARAM_INPUT_AMP_CAPS               (0x0d) // Section 7.3.4.10
#define CC_PARAM_OUTPUT_AMP_CAPS              (0x12) // Section 7.3.4.10
#define CC_PARAM_CONNECTION_LIST_LEN          (0x0e) // Section 7.3.4.11
#define CC_PARAM_SUPPORTED_PWR_STATES         (0x0f) // Section 7.3.4.12
#define CC_PARAM_PROCESSING_CAPS              (0x10) // Section 7.3.4.13
#define CC_PARAM_GPIO_COUNT                   (0x11) // Section 7.3.4.14
#define CC_PARAM_VOLUME_KNOB_CAPS             (0x13) // Section 7.3.4.15

#define CC_GET_CONNECTION_SELECT_CONTROL      CC_SP_GET_VERB(0x01)      // Section 7.3.3.2
#define CC_SET_CONNECTION_SELECT_CONTROL(val) CC_SP_SET_VERB(0x01, val) // Section 7.3.3.2

#define CC_GET_CONNECTION_LIST_ENTRY(offset)  CC_SP_VERB(0xF02, offset) // Section 7.3.3.3

#define CC_GET_PROCESSING_STATE               CC_SP_GET_VERB(0x03)      // Section 7.3.3.4
#define CC_SET_PROCESSING_STATE(val)          CC_SP_SET_VERB(0x03, val) // Section 7.3.3.4

#define CC_GET_COEFFICIENT_INDEX              CC_LP_GET_VERB(0x05)      // Section 7.3.3.5
#define CC_SET_COEFFICIENT_INDEX(val)         CC_LP_SET_VERB(0x05, val) // Section 7.3.3.5

#define CC_GET_PROCESSING_COEFFICIENT         CC_LP_GET_VERB(0x04)      // Section 7.3.3.6
#define CC_SET_PROCESSING_COEFFICIENT(val)    CC_LP_SET_VERB(0x04, val) // Section 7.3.3.6

#define CC_GET_AMPLIFIER_GAIN_MUTE            CC_LP_GET_VERB(0x03)      // Section 7.3.3.7
#define CC_SET_AMPLIFIER_GAIN_MUTE(val)       CC_LP_SET_VERB(0x03, val) // Section 7.3.3.7

#define CC_GET_CONVERTER_FORMAT               CC_LP_GET_VERB(0x02)      // Section 7.3.3.8
#define CC_SET_CONVERTER_FORMAT(val)          CC_LP_SET_VERB(0x02, val) // Section 7.3.3.8

#define CC_GET_DIGITAL_CONV_CONTROL           CC_SP_GET_VERB(0x0D)      // Section 7.3.3.9
#define CC_SET_DIGITAL_CONV_CONTROL_1(val)    CC_SP_SET_VERB(0x0D, val) // Section 7.3.3.9
#define CC_SET_DIGITAL_CONV_CONTROL_2(val)    CC_SP_SET_VERB(0x0E, val) // Section 7.3.3.9
#define CC_SET_DIGITAL_CONV_CONTROL_3(val)    CC_SP_SET_VERB(0x3E, val) // Section 7.3.3.9
#define CC_SET_DIGITAL_CONV_CONTROL_4(val)    CC_SP_SET_VERB(0x3F, val) // Section 7.3.3.9

#define CC_GET_POWER_STATE                    CC_SP_GET_VERB(0x05)      // Section 7.3.3.10
#define CC_SET_POWER_STATE(val)               CC_SP_SET_VERB(0x05, val) // Section 7.3.3.10

#define CC_GET_CONVERTER_STREAM_CHAN          CC_SP_GET_VERB(0x06)      // Section 7.3.3.11
#define CC_SET_CONVERTER_STREAM_CHAN(val)     CC_SP_SET_VERB(0x06, val) // Section 7.3.3.11

#define CC_GET_INPUT_CONV_SDI_SELECT          CC_SP_GET_VERB(0x04)      // Section 7.3.3.12
#define CC_SET_INPUT_CONV_SDI_SELECT(val)     CC_SP_SET_VERB(0x04, val) // Section 7.3.3.12

#define CC_GET_PIN_WIDGET_CTRL                CC_SP_GET_VERB(0x07)      // Section 7.3.3.13
#define CC_SET_PIN_WIDGET_CTRL(val)           CC_SP_SET_VERB(0x07, val) // Section 7.3.3.13

#define CC_GET_UNSOLICITED_RESP_CTRL          CC_SP_GET_VERB(0x08)      // Section 7.3.3.14
#define CC_SET_UNSOLICITED_RESP_CTRL(val)     CC_SP_SET_VERB(0x08, val) // Section 7.3.3.14

#define CC_GET_PIN_SENSE_CTRL                 CC_SP_GET_VERB(0x09)      // Section 7.3.3.15
#define CC_SET_PIN_SENSE_CTRL(val)            CC_SP_SET_VERB(0x09, val) // Section 7.3.3.15

#define CC_GET_EAPD_BTL_ENABLE                CC_SP_GET_VERB(0x0C)      // Section 7.3.3.16
#define CC_SET_EAPD_BTL_ENABLE(val)           CC_SP_SET_VERB(0x0C, val) // Section 7.3.3.16

#define CC_GET_GPI_DATA                       CC_SP_GET_VERB(0x10)      // Section 7.3.3.17
#define CC_SET_GPI_DATA(val)                  CC_SP_SET_VERB(0x10, val) // Section 7.3.3.17

#define CC_GET_GPI_WAKE_ENB_MASK              CC_SP_GET_VERB(0x11)      // Section 7.3.3.18
#define CC_SET_GPI_WAKE_ENB_MASK(val)         CC_SP_SET_VERB(0x11, val) // Section 7.3.3.18

#define CC_GET_GPI_UNSOLICITED_ENB_MASK       CC_SP_GET_VERB(0x12)      // Section 7.3.3.19
#define CC_SET_GPI_UNSOLICITED_ENB_MASK(val)  CC_SP_SET_VERB(0x12, val) // Section 7.3.3.19

#define CC_GET_GPI_STICKY_MASK                CC_SP_GET_VERB(0x13)      // Section 7.3.3.20
#define CC_SET_GPI_STICKY_MASK(val)           CC_SP_SET_VERB(0x13, val) // Section 7.3.3.20

#define CC_GET_GPO_DATA                       CC_SP_GET_VERB(0x14)      // Section 7.3.3.21
#define CC_SET_GPO_DATA(val)                  CC_SP_SET_VERB(0x14, val) // Section 7.3.3.21

#define CC_GET_GPIO_DATA                      CC_SP_GET_VERB(0x15)      // Section 7.3.3.22
#define CC_SET_GPIO_DATA(val)                 CC_SP_SET_VERB(0x15, val) // Section 7.3.3.22

#define CC_GET_GPIO_ENB_MASK                  CC_SP_GET_VERB(0x16)      // Section 7.3.3.23
#define CC_SET_GPIO_ENB_MASK(val)             CC_SP_SET_VERB(0x16, val) // Section 7.3.3.23

#define CC_GET_GPIO_DIR                       CC_SP_GET_VERB(0x17)      // Section 7.3.3.24
#define CC_SET_GPIO_DIR(val)                  CC_SP_SET_VERB(0x17, val) // Section 7.3.3.24

#define CC_GET_GPIO_WAKE_ENB_MASK             CC_SP_GET_VERB(0x18)      // Section 7.3.3.25
#define CC_SET_GPIO_WAKE_ENB_MASK(val)        CC_SP_SET_VERB(0x18, val) // Section 7.3.3.25

#define CC_GET_GPIO_UNSOLICITED_ENB_MASK      CC_SP_GET_VERB(0x19)      // Section 7.3.3.26
#define CC_SET_GPIO_UNSOLICITED_ENB_MASK(val) CC_SP_SET_VERB(0x19, val) // Section 7.3.3.26

#define CC_GET_GPIO_STICKY_MASK               CC_SP_GET_VERB(0x1a)      // Section 7.3.3.27
#define CC_SET_GPIO_STICKY_MASK(val)          CC_SP_SET_VERB(0x1a, val) // Section 7.3.3.27

#define CC_GET_BEEP_GENERATION                CC_SP_GET_VERB(0x0a)      // Section 7.3.3.28
#define CC_SET_BEEP_GENERATION(val)           CC_SP_SET_VERB(0x0a, val) // Section 7.3.3.28

#define CC_GET_VOLUME_KNOB                    CC_SP_GET_VERB(0x0f)      // Section 7.3.3.29
#define CC_SET_VOLUME_KNOB(val)               CC_SP_SET_VERB(0x0f, val) // Section 7.3.3.29

#define CC_GET_IMPLEMENTATION_ID              CC_SP_GET_VERB(0x20)      // Section 7.3.3.30
#define CC_SET_IMPLEMENTATION_ID_1(val)       CC_SP_SET_VERB(0x20, val) // Section 7.3.3.30
#define CC_SET_IMPLEMENTATION_ID_2(val)       CC_SP_SET_VERB(0x21, val) // Section 7.3.3.30
#define CC_SET_IMPLEMENTATION_ID_3(val)       CC_SP_SET_VERB(0x22, val) // Section 7.3.3.30
#define CC_SET_IMPLEMENTATION_ID_4(val)       CC_SP_SET_VERB(0x23, val) // Section 7.3.3.30

#define CC_GET_CONFIG_DEFAULT                 CC_SP_GET_VERB(0x1c)      // Section 7.3.3.31
#define CC_SET_CONFIG_DEFAULT_1(val)          CC_SP_SET_VERB(0x1c, val) // Section 7.3.3.31
#define CC_SET_CONFIG_DEFAULT_2(val)          CC_SP_SET_VERB(0x1d, val) // Section 7.3.3.31
#define CC_SET_CONFIG_DEFAULT_3(val)          CC_SP_SET_VERB(0x1e, val) // Section 7.3.3.31
#define CC_SET_CONFIG_DEFAULT_4(val)          CC_SP_SET_VERB(0x1f, val) // Section 7.3.3.31

#define CC_GET_STRIPE_CONTROL                 CC_SP_GET_VERB(0x24)      // Section 7.3.3.32
#define CC_SET_STRIPE_CONTROL(val)            CC_SP_SET_VERB(0x24, val) // Section 7.3.3.32

#define CC_EXECUTE_FUNCTION_RESET             CC_SP_SET_VERB(0xFF, 0)   // Section 7.3.3.33

#define CC_GET_EDID_LIKE_DATA                 CC_SP_GET_VERB(0x2F)      // Section 7.3.3.34

#define CC_GET_CONV_CHANNEL_COUNT             CC_SP_GET_VERB(0x2d)      // Section 7.3.3.35
#define CC_SET_CONV_CHANNEL_COUNT(val)        CC_SP_SET_VERB(0x2d, val) // Section 7.3.3.35

#define CC_GET_DIP_SIZE                       CC_SP_GET_VERB(0x2e)      // Section 7.3.3.36

#define CC_GET_DIP_INDEX                      CC_SP_GET_VERB(0x30)      // Section 7.3.3.37
#define CC_SET_DIP_INDEX(val)                 CC_SP_SET_VERB(0x30, val) // Section 7.3.3.37

#define CC_GET_DIP_DATA                       CC_SP_GET_VERB(0x31)      // Section 7.3.3.38
#define CC_SET_DIP_DATA(val)                  CC_SP_SET_VERB(0x31, val) // Section 7.3.3.38

#define CC_GET_DIP_XMIT_CTRL                  CC_SP_GET_VERB(0x32)      // Section 7.3.3.39
#define CC_SET_DIP_XMIT_CTRL(val)             CC_SP_SET_VERB(0x32, val) // Section 7.3.3.39

#define CC_GET_CP_CONTROL                     CC_SP_GET_VERB(0x33)      // Section 7.3.3.40
#define CC_SET_CP_CONTROL(val)                CC_SP_SET_VERB(0x33, val) // Section 7.3.3.40

#define CC_GET_ASP_CHAN_MAPPING               CC_SP_GET_VERB(0x34)      // Section 7.3.3.41
#define CC_SET_ASP_CHAN_MAPPING(val)          CC_SP_SET_VERB(0x34, val) // Section 7.3.3.41
