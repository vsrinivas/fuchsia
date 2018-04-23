// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

////////////////////////////////////////////////////
//
// General Audio interface constants
//
////////////////////////////////////////////////////

// audio interface subclasses
#define USB_SUBCLASS_AUDIO_CONTROL              0x01
#define USB_SUBCLASS_AUDIO_STREAMING            0x02
#define USB_SUBCLASS_MIDI_STREAMING             0x03

// audio class specific descriptor types
#define USB_AUDIO_CS_DEVICE                     0x21
#define USB_AUDIO_CS_CONFIGURATION              0x22
#define USB_AUDIO_CS_STRING                     0x23
#define USB_AUDIO_CS_INTERFACE                  0x24
#define USB_AUDIO_CS_ENDPOINT                   0x25

////////////////////////////////////////////////////
//
// Audio Control interface constants
//
////////////////////////////////////////////////////

// audio class specific AC interface descriptor subtypes
#define USB_AUDIO_AC_HEADER                     0x01
#define USB_AUDIO_AC_INPUT_TERMINAL             0x02
#define USB_AUDIO_AC_OUTPUT_TERMINAL            0x03
#define USB_AUDIO_AC_MIXER_UNIT                 0x04
#define USB_AUDIO_AC_SELECTOR_UNIT              0x05
#define USB_AUDIO_AC_FEATURE_UNIT               0x06
#define USB_AUDIO_AC_PROCESSING_UNIT            0x07
#define USB_AUDIO_AC_EXTENSION_UNIT             0x08

// processing unit process types
#define USB_AUDIO_UP_DOWN_MIX_PROCESS           0x01
#define USB_AUDIO_DOLBY_PROLOGIC_PROCESS        0x02
#define USB_AUDIO_3D_STEREO_EXTENDER_PROCESS    0x03
#define USB_AUDIO_REVERBERATION_PROCESS         0x04
#define USB_AUDIO_CHORUS_PROCESS                0x05
#define USB_AUDIO_DYN_RANGE_COMP_PROCESS        0x06

// audio class specific endpoint descriptor subtypes
#define USB_AUDIO_EP_GENERAL                    0x01

// audio class specific request codes
#define USB_AUDIO_SET_CUR                       0x01
#define USB_AUDIO_GET_CUR                       0x81
#define USB_AUDIO_SET_MIN                       0x02
#define USB_AUDIO_GET_MIN                       0x82
#define USB_AUDIO_SET_MAX                       0x03
#define USB_AUDIO_GET_MAX                       0x83
#define USB_AUDIO_SET_RES                       0x04
#define USB_AUDIO_GET_RES                       0x84
#define USB_AUDIO_SET_MEM                       0x05
#define USB_AUDIO_GET_MEM                       0x85
#define USB_AUDIO_GET_STAT                      0xFF

// terminal control selectors
#define USB_AUDIO_COPY_PROTECT_CONTROL          0x01

// feature unit control selectors
#define USB_AUDIO_MUTE_CONTROL                  0x01
#define USB_AUDIO_VOLUME_CONTROL                0x02
#define USB_AUDIO_BASS_CONTROL                  0x03
#define USB_AUDIO_MID_CONTROL                   0x04
#define USB_AUDIO_TREBLE_CONTROL                0x05
#define USB_AUDIO_GRAPHIC_EQUALIZER_CONTROL     0x06
#define USB_AUDIO_AUTOMATIC_GAIN_CONTROL        0x07
#define USB_AUDIO_DELAY_CONTROL                 0x08
#define USB_AUDIO_BASS_BOOST_CONTROL            0x09
#define USB_AUDIO_LOUDNESS_CONTROL              0x0A

// feature unit control support bitmasks
#define USB_AUDIO_FU_BMA_MUTE                   (1u << 0u)
#define USB_AUDIO_FU_BMA_VOLUME                 (1u << 1u)
#define USB_AUDIO_FU_BMA_BASS                   (1u << 2u)
#define USB_AUDIO_FU_BMA_MID                    (1u << 3u)
#define USB_AUDIO_FU_BMA_TREBLE                 (1u << 4u)
#define USB_AUDIO_FU_BMA_GRAPHIC_EQUALIZER      (1u << 5u)
#define USB_AUDIO_FU_BMA_AUTOMATIC_GAIN         (1u << 6u)
#define USB_AUDIO_FU_BMA_DELAY                  (1u << 7u)
#define USB_AUDIO_FU_BMA_BASS_BOOST             (1u << 8u)
#define USB_AUDIO_FU_BMA_LOUDNESS               (1u << 9u)

// up/down mix processing unit control selectors
#define USB_AUDIO_UD_ENABLE_CONTROL             0x01
#define USB_AUDIO_UD_MODE_SELECT_CONTROL        0x02
#define USB_AUDIO_UD_MODE_SELECT_CONTROL        0x02

// Dolby Prologic processing unit control selectors
#define USB_AUDIO_DP_ENABLE_CONTROL             0x01
#define USB_AUDIO_DP_MODE_SELECT_CONTROL        0x02

// 3D stereo extender processing unit control selectors
#define USB_AUDIO_3D_ENABLE_CONTROL             0x01
#define USB_AUDIO_SPACIOUSNESS_CONTROL          0x03

// reverberation processing unit control selectors
#define USB_AUDIO_RV_ENABLE_CONTROL             0x01
#define USB_AUDIO_REVERB_LEVEL_CONTROL          0x02
#define USB_AUDIO_REVERB_TIME_CONTROL           0x03
#define USB_AUDIO_REVERB_FEEDBACK_CONTROL       0x04

// chorus processing unit control selectors
#define USB_AUDIO_CH_ENABLE_CONTROL             0x01
#define USB_AUDIO_CHORUS_LEVEL_CONTROL          0x02
#define USB_AUDIO_CHORUS_RATE_CONTROL           0x03
#define USB_AUDIO_CHORUS_DEPTH_CONTROL          0x04

// dynamic range compressor processing unit control selectors
#define USB_AUDIO_DR_ENABLE_CONTROL             0x01
#define USB_AUDIO_COMPRESSION_RATE_CONTROL      0x02
#define USB_AUDIO_MAXAMPL_CONTROL               0x03
#define USB_AUDIO_THRESHOLD_CONTROL             0x04
#define USB_AUDIO_ATTACK_TIME                   0x05
#define USB_AUDIO_RELEASE_TIME                  0x06

// extension unit control selectors
#define USB_AUDIO_XU_ENABLE_CONTROL             0x01

// endpoint control selectors
#define USB_AUDIO_SAMPLING_FREQ_CONTROL         0x01
#define USB_AUDIO_PITCH_CONTROL                 0x02

// USB audio terminal types
#define USB_AUDIO_TERMINAL_USB_UNDEFINED                0x0100
#define USB_AUDIO_TERMINAL_USB_STREAMING                0x0101
#define USB_AUDIO_TERMINAL_USB_VENDOR                   0x01FF
#define USB_AUDIO_TERMINAL_INPUT_UNDEFINED              0x0200
#define USB_AUDIO_TERMINAL_MICROPHONE                   0x0201
#define USB_AUDIO_TERMINAL_DESKTOP_MICROPHONE           0x0202
#define USB_AUDIO_TERMINAL_PERSONAL_MICROPHONE          0x0203
#define USB_AUDIO_TERMINAL_OMNI_DIRECTIONAL_MICROPHONE  0x0204
#define USB_AUDIO_TERMINAL_MICROPHONE_ARRAY             0x0205
#define USB_AUDIO_TERMINAL_PROCESSING_MICROPHONE_ARRAY  0x0206
#define USB_AUDIO_TERMINAL_OUTPUT_UNDEFINED             0x0300
#define USB_AUDIO_TERMINAL_SPEAKER                      0x0301
#define USB_AUDIO_TERMINAL_HEADPHONES                   0x0302
#define USB_AUDIO_TERMINAL_HEAD_MOUNTED_DISPLAY_AUDIO   0x0303
#define USB_AUDIO_TERMINAL_DESKTOP_SPEAKER              0x0304
#define USB_AUDIO_TERMINAL_ROOM_SPEAKER                 0x0305
#define USB_AUDIO_TERMINAL_COMMUNICATION_SPEAKER        0x0306
#define USB_AUDIO_TERMINAL_LOW_FREQ_EFFECTS_SPEAKER     0x0307
#define USB_AUDIO_TERMINAL_BIDIRECTIONAL_UNDEFINED      0x0400
#define USB_AUDIO_TERMINAL_HANDSET                      0x0401
#define USB_AUDIO_TERMINAL_HEADSET                      0x0402
#define USB_AUDIO_TERMINAL_SPEAKERPHONE                 0x0403
#define USB_AUDIO_TERMINAL_ECHO_SUPPRESSING_SPEAKERPHONE 0x0404
#define USB_AUDIO_TERMINAL_ECHO_CANCELING_SPEAKERPHONE  0x0405
#define USB_AUDIO_TERMINAL_TELEPHONY_UNDEFINED          0x0500
#define USB_AUDIO_TERMINAL_PHONE_LINE                   0x0501
#define USB_AUDIO_TERMINAL_TELEPHONE                    0x0502
#define USB_AUDIO_TERMINAL_DOWN_LINE_PHONE              0x0503
#define USB_AUDIO_TERMINAL_EXTERNAL_UNDEFINED           0x0600
#define USB_AUDIO_TERMINAL_ANALOG_CONNECTOR             0x0601
#define USB_AUDIO_TERMINAL_DIGITAL_AUDIO_INTERFACE      0x0602
#define USB_AUDIO_TERMINAL_LINE_CONNECTOR               0x0603
#define USB_AUDIO_TERMINAL_LEGACY_AUDIO_CONNECTOR       0x0604
#define USB_AUDIO_TERMINAL_SPDIF_INTERFACE              0x0605
#define USB_AUDIO_TERMINAL_1394_DA_STREAM               0x0606
#define USB_AUDIO_TERMINAL_1394_DV_STREAM_SOUNDTRACK    0x0607
#define USB_AUDIO_TERMINAL_EMBEDDED_UNDEFINED           0x0700
#define USB_AUDIO_TERMINAL_LEVEL_CALIBRATION_NOISE_SOURCE 0x0701
#define USB_AUDIO_TERMINAL_EQUALIZATION_NOISE           0x0702
#define USB_AUDIO_TERMINAL_CD_PLAYER                    0x0703
#define USB_AUDIO_TERMINAL_DAT                          0x0704
#define USB_AUDIO_TERMINAL_DCC                          0x0705
#define USB_AUDIO_TERMINAL_MINI_DISK                    0x0706
#define USB_AUDIO_TERMINAL_ANALOG_TAPE                  0x0707
#define USB_AUDIO_TERMINAL_PHONOGRAPH                   0x0708
#define USB_AUDIO_TERMINAL_VCR_AUDIO                    0x0709
#define USB_AUDIO_TERMINAL_VIDEO_DISK_AUDIO             0x070A
#define USB_AUDIO_TERMINAL_DVD_AUDIO                    0x070B
#define USB_AUDIO_TERMINAL_TV_TUNER_AUDIO               0x070C
#define USB_AUDIO_TERMINAL_SATELLITE_RECEIVER_AUDIO     0x070D
#define USB_AUDIO_TERMINAL_CABLE_TUNER_AUDIO            0x070E
#define USB_AUDIO_TERMINAL_DSS_AUDIO                    0x070F
#define USB_AUDIO_TERMINAL_RADIO_RECEIVER               0x0710
#define USB_AUDIO_TERMINAL_RADIO_TRANSMITTER            0x0711
#define USB_AUDIO_TERMINAL_MULTI_TRACK_RECORDER         0x0712
#define USB_AUDIO_TERMINAL_SYNTHESIZER                  0x0713

////////////////////////////////////////////////////
//
// Audio streaming interface constants
//
////////////////////////////////////////////////////

// Audio stream class-specific AS interface descriptor subtypes
#define USB_AUDIO_AS_GENERAL                    0x01
#define USB_AUDIO_AS_FORMAT_TYPE                0x02
#define USB_AUDIO_AS_FORMAT_SPECIFIC            0x03

// wFormatTag values present in the class specific AS header
// Defined in Section A.1 of USB Device Class Definition for Audio Data Formats
#define USB_AUDIO_AS_FT_TYPE_I_UNDEFINED        0x0000
#define USB_AUDIO_AS_FT_PCM                     0x0001
#define USB_AUDIO_AS_FT_PCM8                    0x0002
#define USB_AUDIO_AS_FT_IEEE_FLOAT              0x0003
#define USB_AUDIO_AS_FT_ALAW                    0x0004
#define USB_AUDIO_AS_FT_MULAW                   0x0005
#define USB_AUDIO_AS_FT_TYPE_II_UNDEFINED       0x1000
#define USB_AUDIO_AS_FT_MPEG                    0x1001
#define USB_AUDIO_AS_FT_AC3                     0x1002
#define USB_AUDIO_AS_FT_TYPE_III_UNDEFINED      0x2000
#define USB_AUDIO_AS_FT_IEC1937_AC3             0x2001
#define USB_AUDIO_AS_FT_IEC1937_MPEG1_L1        0x2002
#define USB_AUDIO_AS_FT_IEC1937_MPEG1_L23       0x2003
#define USB_AUDIO_AS_FT_IEC1937_MPEG2_EXT       0x2004
#define USB_AUDIO_AS_FT_IEC1937_MPEG2_L1_LS     0x2005
#define USB_AUDIO_AS_FT_IEC1937_MPEG2_L23_LS    0x2006

// Audio stream class-specific format-specific types
#define USB_AUDIO_FORMAT_TYPE_UNDEFINED         0x00
#define USB_AUDIO_FORMAT_TYPE_I                 0x01
#define USB_AUDIO_FORMAT_TYPE_II                0x02
#define USB_AUDIO_FORMAT_TYPE_III               0x03

////////////////////////////////////////////////////
//
// MIDI streaming interface constants
//
////////////////////////////////////////////////////

// MIDI class specific MS interface descriptor subtypes
#define USB_MIDI_MS_HEADER                      0x01
#define USB_MIDI_IN_JACK                        0x02
#define USB_MIDI_OUT_JACK                       0x03
#define USB_MIDI_ELEMENT                        0x04

// MIDI class specific MS endpoint descriptor subtypes
#define USB_MIDI_MS_GENERAL                     0x01

// MIDI IN and OUT jack types
#define USB_MIDI_JACK_EMBEDDED                  0x01
#define USB_MIDI_JACK_INTERNAL                  0x02

// MIDI endpoint control selectors
#define USB_MIDI_ASSOCIATION_CONTROL            0x01


// Top level header structure shared by all USB audio descriptors.
//
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_INTERFACE
    uint8_t bDescriptorSubtype;
} __PACKED usb_audio_desc_header;

// Audio Control Interface descriptor definitions
//
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_AUDIO_AC_HEADER
    uint16_t bcdADC;
    uint16_t wTotalLength;
    uint8_t bInCollection;
    uint8_t baInterfaceNr[];
} __PACKED usb_audio_ac_header_desc;

// Common header structure shared by all unit and terminal descriptors found in
// an Audio Control interface descriptor.
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_AUDIO_AC_.*_(TERMINAL|UNIT)
    uint8_t bID;
} __PACKED usb_audio_ac_ut_desc;

// Common header structure shared by all terminal descriptors found in an Audio
// Control interface descriptor.
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_AUDIO_AC_(INPUT|OUTPUT)_TERMINAL
    uint8_t bTerminalID;
    uint16_t wTerminalType;
    uint8_t bAssocTerminal;
} __PACKED usb_audio_ac_terminal_desc;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_AUDIO_AC_INPUT_TERMINAL
    uint8_t bTerminalID;
    uint16_t wTerminalType;
    uint8_t bAssocTerminal;
    uint8_t bNrChannels;
    uint16_t wChannelConfig;
    uint8_t iChannelNames;
    uint8_t iTerminal;
} __PACKED usb_audio_ac_input_terminal_desc;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_AUDIO_AC_OUTPUT_TERMINAL
    uint8_t bTerminalID;
    uint16_t wTerminalType;
    uint8_t bAssocTerminal;
    uint8_t bSourceID;
    uint8_t iTerminal;
} __PACKED usb_audio_ac_output_terminal_desc;

// Note: Mixer unit descriptors contain two inlined variable length arrays, each
// with descriptor data following them.  They are therefor described using 3
// structure definitions which are logically concatenated, but separated by the
// inline arrays.
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_AUDIO_AC_MIXER_UNIT
    uint8_t bUnitID;
    uint8_t bNrInPins;
    uint8_t baSourceID[];
} __PACKED usb_audio_ac_mixer_unit_desc_0;

typedef struct {
    uint8_t bNrChannels;
    uint16_t wChannelConfig;
    uint8_t iChannelNames;
    uint8_t bmControls[];
} __PACKED usb_audio_ac_mixer_unit_desc_1;

typedef struct {
    uint8_t iMixer;
} __PACKED usb_audio_ac_mixer_unit_desc_2;

// Note: Selector unit descriptors contain an inlined variable length array with
// descriptor data following it.  They are therefor described using 2 structure
// definitions which are logically concatenated, but separated by the inline
// array.
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_AUDIO_AC_SELECTOR_UNIT
    uint8_t bUnitID;
    uint8_t bNrInPins;
    uint8_t baSourceID[];
} __PACKED usb_audio_ac_selector_unit_desc_0;

typedef struct {
    uint8_t iSelector;
} __PACKED usb_audio_ac_selector_unit_desc_1;

// Note: Feature unit descriptors contain an inlined variable length array with
// descriptor data following it.  They are therefor described using 2 structure
// definitions which are logically concatenated, but separated by the inline
// array.
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_AUDIO_AC_FEATURE_UNIT
    uint8_t bUnitID;
    uint8_t bSourceID;
    uint8_t bControlSize;
    uint8_t bmaControls[];
} __PACKED usb_audio_ac_feature_unit_desc_0;

typedef struct {
    uint8_t iFeature;
} __PACKED usb_audio_ac_feature_unit_desc_1;

// Note: Processing unit descriptors contain two inlined variable length arrays,
// each with descriptor data following them.  They are therefor described using
// 3 structure definitions which are logically concatinated, but separated by
// the inline arrays.
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_AUDIO_AC_PROCESSING_UNIT
    uint8_t bUnitID;
    uint16_t wProcessType;
    uint8_t bNrInPins;
    uint8_t baSourceID[];
} __PACKED usb_audio_ac_processing_unit_desc_0;

typedef struct {
    uint8_t bNrChannels;
    uint16_t wChannelConfig;
    uint8_t iChannelNames;
    uint8_t bControlSize;
    uint8_t bmControls[];
} __PACKED usb_audio_ac_processing_unit_desc_1;

typedef struct {
    uint8_t iProcessing;
    // Note: The Process-specific control structure follows this with the
    // structure type determined by wProcessType
    // TODO(johngro) : Define the process specific control structures.  As of
    // the 1.0 revision of the USB audio spec, the types to be defined are...
    //
    // ** Up/Down-mix
    // ** Dolby Prologic
    // ** 3D-Stereo Extender
    // ** Reverberation
    // ** Chorus
    // ** Dynamic Range Compressor
} __PACKED usb_audio_ac_processing_unit_desc_2;

// Note: Extension unit descriptors contain two inlined variable length arrays,
// each with descriptor data following them.  They are therefor described using
// 3 structure definitions which are logically concatenated, but separated by
// the inline arrays.
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_AUDIO_AC_EXTENSION_UNIT
    uint8_t bUnitID;
    uint16_t wExtensionCode;
    uint8_t bNrInPins;
    uint8_t baSourceID[];
} __PACKED usb_audio_ac_extension_unit_desc_0;

typedef struct {
    uint8_t bNrChannels;
    uint16_t wChannelConfig;
    uint8_t iChannelNames;
    uint8_t bControlSize;
    uint8_t bmControls[];
} __PACKED usb_audio_ac_extension_unit_desc_1;

typedef struct {
    uint8_t iExtension;
} __PACKED usb_audio_ac_extension_unit_desc_2;

// Audio Streaming Interface descriptor definitions
//
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_AUDIO_AS_GENERAL
    uint8_t bTerminalLink;
    uint8_t bDelay;
    uint16_t wFormatTag;
} __PACKED usb_audio_as_header_desc;

typedef struct {
    uint8_t freq[3];            // 24 bit unsigned integer, little-endian
} __PACKED usb_audio_as_samp_freq;

// Common header used by all format type descriptors
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_AUDIO_AS_FORMAT_TYPE
    uint8_t bFormatType;
} __PACKED usb_audio_as_format_type_hdr;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_AUDIO_AS_FORMAT_TYPE
    uint8_t bFormatType;            // USB_AUDIO_FORMAT_TYPE_I
    uint8_t bNrChannels;
    uint8_t bSubFrameSize;
    uint8_t bBitResolution;
    uint8_t bSamFreqType;           // number of sampling frequencies
    usb_audio_as_samp_freq tSamFreq[]; // list of sampling frequencies (3 bytes each)
} __PACKED usb_audio_as_format_type_i_desc;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_ENDPOINT
    uint8_t bDescriptorSubtype;     // USB_AUDIO_EP_GENERAL
    uint8_t bmAttributes;
    uint8_t bLockDelayUnits;
    uint16_t wLockDelay;
} __PACKED usb_audio_as_isoch_ep_desc;

// MIDI Streaming Interface descriptor definitions
//
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_MIDI_MS_HEADER
    uint16_t bcdMSC;
    uint16_t wTotalLength;
} __PACKED usb_midi_ms_header_desc;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_MIDI_IN_JACK
    uint8_t bJackType;
    uint8_t bJackID;
    uint8_t iJack;
} __PACKED usb_midi_ms_in_jack_desc;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_MIDI_OUT_JACK
    uint8_t bJackType;
    uint8_t bJackID;
    uint8_t bNrInputPins;
    uint8_t baSourceID;
    uint8_t baSourcePin;
} __PACKED usb_midi_ms_out_jack_desc;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_AUDIO_CS_ENDPOINT
    uint8_t bDescriptorSubtype;     // USB_MIDI_MS_GENERAL
    uint8_t bNumEmbMIDIJack;
    uint8_t baAssocJackID[];
} __PACKED usb_midi_ms_endpoint_desc;

__END_CDECLS;
