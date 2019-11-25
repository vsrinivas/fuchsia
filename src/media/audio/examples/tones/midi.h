// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_EXAMPLES_TONES_MIDI_H_
#define SRC_MEDIA_AUDIO_EXAMPLES_TONES_MIDI_H_

// clang-format off

#define MIDI_COMMAND_MASK           0xF0
#define MIDI_CHANNEL_MASK           0x0F

// Channel voice messages.
#define MIDI_NOTE_OFF               0x80
#define MIDI_NOTE_ON                0x90
#define MIDI_POLYPHONIC_AFTERTOUCH  0xA0
#define MIDI_CONTROL_CHANGE         0xB0
#define MIDI_PROGRAM_CHANGE         0xC0
#define MIDI_CHANNEL_PRESSURE       0xD0
#define MIDI_PITCH_BEND             0xE0

#define MIDI_NOTE_NUMBER_MASK       0x7F
#define MIDI_NOTE_VELOCITY_MASK     0x7F

// System Common Messages.
#define MIDI_SYSTEM_EXCLUSIVE       0xF0
#define MIDI_MIDI_TIME_CODE         0xF1
#define MIDI_SONG_POSITION          0xF2
#define MIDI_SONG_SELECT            0xF3
#define MIDI_TUNE_REQUEST           0xF6
#define MIDI_END_SYSEX              0xF7

// System Real-Time Messages
#define MIDI_TIMING_CLOCK           0xF8
#define MIDI_START                  0xFA
#define MIDI_CONTINUE               0xFB
#define MIDI_STOP                   0xFC
#define MIDI_ACTIVE_SENSING         0xFE
#define MIDI_RESET                  0xFF

// MIDI note 69 corresponds to A above middle C
#define MIDI_REF_FREQUENCY          440.0
#define MIDI_REF_INDEX              69

// clang-format on

#endif  // SRC_MEDIA_AUDIO_EXAMPLES_TONES_MIDI_H_
