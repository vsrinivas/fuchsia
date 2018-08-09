// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_MEDIA_TONES_TONES_H_
#define GARNET_EXAMPLES_MEDIA_TONES_TONES_H_

#include <list>
#include <map>

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/fzl/vmo-mapper.h>

#include "garnet/examples/media/tones/tone_generator.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"

namespace examples {

class MidiKeyboard;

class Tones {
 public:
  Tones(bool interactive, fit::closure quit_callback);

  ~Tones();

 private:
  friend class MidiKeyboard;

  // Quits the app.
  void Quit();

  // Calls |HandleKeystroke| on the message loop when console input is ready.
  void WaitForKeystroke();

  // Handles a keystroke, possibly calling |WaitForKeystroke| to wait for the
  // next one.
  void HandleKeystroke();

  // Handles a note on/off event from a midi keyboard for the specified note.  0
  // corresponds to middle C, every tick above or below is a distance of 1/2 a
  // step.  So, -1 would be the B below middle C, while 3 would be the D# above
  // middle C.
  void HandleMidiNote(int note, int velocity, bool note_on);

  // Adds notes to the score.
  void BuildScore();

  // Handle a change in the minimum lead time requirement, starting playback if
  // needed..
  void OnMinLeadTimeChanged(int64_t min_lead_time_nsec);

  // Sends as much content as is currently demanded. Ends the stream when all
  // content has been sent.
  void SendPackets();

  // Fills |buffer| with audio.
  void FillBuffer(float* buffer);

  // Determines whether all audio has been sent.
  bool done() const {
    return !interactive_ && frequencies_by_pts_.empty() &&
           tone_generators_.empty();
  }

  bool interactive_;
  fit::closure quit_callback_;
  fsl::FDWaiter fd_waiter_;
  fuchsia::media::AudioOutPtr audio_renderer_;
  std::map<int64_t, float> frequencies_by_pts_;
  std::list<ToneGenerator> tone_generators_;
  int64_t pts_ = 0;
  fzl::VmoMapper payload_buffer_;
  uint32_t active_packets_in_flight_ = 0;
  uint32_t target_packets_in_flight_ = 0;
  bool started_ = false;
  std::unique_ptr<MidiKeyboard> midi_keyboard_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Tones);
};

}  // namespace examples

#endif  // GARNET_EXAMPLES_MEDIA_TONES_TONES_H_
