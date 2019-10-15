// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_AUDIO_USB_AUDIO_USB_AUDIO_PATH_H_
#define ZIRCON_SYSTEM_DEV_AUDIO_USB_AUDIO_USB_AUDIO_PATH_H_

#include <memory>
#include <utility>

#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>

#include "usb-audio-units.h"
#include "usb-audio.h"

namespace audio {
namespace usb {

class UsbAudioControlInterface;

// A small container class used by the audio control interface for describing a
// path through the unit/terminal graph from host to pin (or vice-versa)
class AudioPath : public fbl::DoublyLinkedListable<std::unique_ptr<AudioPath>> {
 public:
  Direction direction() const { return direction_; }
  const Terminal& stream_terminal() const {
    // If we do not have a stashed pointer to our terminal yet, then someone
    // is calling this accessor before Setup completed successfully.  This
    // should never happen.
    ZX_DEBUG_ASSERT(stream_terminal_ != nullptr);
    return *stream_terminal_;
  }

  // clang-format off
    bool  has_gain() const { return feature_unit_ && feature_unit_->has_vol(); }
    bool  has_agc()  const { return feature_unit_ && feature_unit_->has_agc(); }
    bool  has_mute() const { return feature_unit_ && feature_unit_->has_mute(); }
    float cur_gain() const { return feature_unit_ ? feature_unit_->vol_cur_db() : 0.0f; }
    float min_gain() const { return feature_unit_ ? feature_unit_->vol_min_db() : 0.0f; }
    float max_gain() const { return feature_unit_ ? feature_unit_->vol_max_db() : 0.0f; }
    float gain_res() const { return feature_unit_ ? feature_unit_->vol_res_db() : 0.0f; }
    bool  cur_agc()  const { return feature_unit_ ? feature_unit_->agc_cur() : false; }
    bool  cur_mute() const { return feature_unit_ ? feature_unit_->mute_cur() : false; }
  // clang-format on

  float SetGain(const usb_protocol_t& proto, float db) {
    return feature_unit_ ? feature_unit_->SetVol(proto, db) : 0.0f;
  }

  bool SetMute(const usb_protocol_t& proto, bool mute) {
    return feature_unit_ ? feature_unit_->SetMute(proto, mute) : false;
  }

  bool SetAgc(const usb_protocol_t& proto, bool enabled) {
    return feature_unit_ ? feature_unit_->SetAgc(proto, enabled) : false;
  }

 private:
  friend class std::default_delete<AudioPath>;
  friend class UsbAudioControlInterface;

  // Methods use by the audio control interface class to build audio paths
  // during its walk of the unit/terminal graph.  Basically, the control
  // interface class calls...
  //
  // 1) 'Create' when it finds what looks like a valid path during its recursive
  //    walk of the graph.
  // 2) 'AddUnit' as it unwinds from the walk in order to store references
  //    which form the path in the proper order inside of the path.
  // 3) 'Setup' when it is finished in order to sanity check the path and to
  //    stash pointers to important elements, such as the stream terminal
  //    node and the feature unit node (if found).
  //
  static std::unique_ptr<AudioPath> Create(uint32_t unit_count);
  void AddUnit(uint32_t ndx, fbl::RefPtr<AudioUnit> unit);
  zx_status_t Setup(const usb_protocol_t& proto);

  AudioPath(std::unique_ptr<fbl::RefPtr<AudioUnit>[]> units, uint32_t unit_count)
      : units_(std::move(units)), unit_count_(unit_count) {}
  ~AudioPath() {}

  DISALLOW_COPY_ASSIGN_AND_MOVE(AudioPath);

  const std::unique_ptr<fbl::RefPtr<AudioUnit>[]> units_;
  const uint32_t unit_count_;
  Direction direction_ = Direction::Unknown;

  // Note: Strictly speaking, these cached references do not have to be
  // RefPtrs.  In theory, the members of units_ should always outlive these
  // cache references.   This said, the cost of holding an extra reference on
  // the objects is basically zero, and storing the pointers internally as
  // RefPtr<>s makes it easy to know that this is safe from a lifecycle
  // perspective, if perhaps a tiny bit paranoid.
  fbl::RefPtr<const Terminal> stream_terminal_;
  fbl::RefPtr<FeatureUnit> feature_unit_;
};

}  // namespace usb
}  // namespace audio

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_USB_AUDIO_USB_AUDIO_PATH_H_
