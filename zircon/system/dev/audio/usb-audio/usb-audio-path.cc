// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-audio-path.h"

#include <memory>
#include <utility>

#include <fbl/alloc_checker.h>

#include "debug-logging.h"

namespace audio {
namespace usb {

std::unique_ptr<AudioPath> AudioPath::Create(uint32_t unit_count) {
  fbl::AllocChecker ac;

  std::unique_ptr<fbl::RefPtr<AudioUnit>[]> units(new (&ac) fbl::RefPtr<AudioUnit>[unit_count]);
  if (!ac.check()) {
    GLOBAL_LOG(ERROR, "Failed to allocate %u units for AudioPath!", unit_count);
    return nullptr;
  }

  std::unique_ptr<AudioPath> ret(new (&ac) AudioPath(std::move(units), unit_count));
  if (!ac.check()) {
    GLOBAL_LOG(ERROR, "Failed to allocate AudioPath!");
    return nullptr;
  }

  return ret;
}

void AudioPath::AddUnit(uint32_t ndx, fbl::RefPtr<AudioUnit> unit) {
  ZX_DEBUG_ASSERT(ndx < unit_count_);
  ZX_DEBUG_ASSERT(unit != nullptr);
  units_[ndx] = std::move(unit);
}

zx_status_t AudioPath::Setup(const usb_protocol_t& proto) {
  // If setup is being called, we should have allocated a units_ array, and it
  // must be a minimum of 2 units long (the input and the output terminal).
  // All of its members must be non-null.  The first element in the array must
  // be an output terminal while the last element in the array must be an
  // input terminal.  Check all of this before proceeding.
  if ((units_ == nullptr) || (unit_count_ < 2)) {
    GLOBAL_LOG(ERROR, "Bad units array during %s (ptr %p, count %u)\n", __PRETTY_FUNCTION__,
               units_.get(), unit_count_);
    return ZX_ERR_INTERNAL;
  }

  for (uint32_t i = 0; i < unit_count_; ++i) {
    if (units_[i] == nullptr) {
      GLOBAL_LOG(ERROR, "Empty unit slot %s (ndx %u)\n", __PRETTY_FUNCTION__, i);
      return ZX_ERR_INTERNAL;
    }
  }

  const auto& first_unit = *units_[0];
  if (first_unit.type() != AudioUnit::Type::OutputTerminal) {
    GLOBAL_LOG(ERROR,
               "First element of audio path must be an OutputTerminal, "
               "but a unit of type \"%s\" was discovered instead!\n",
               first_unit.type_name());
    return ZX_ERR_INTERNAL;
  }

  const auto& last_unit = *units_[unit_count_ - 1];
  if (last_unit.type() != AudioUnit::Type::InputTerminal) {
    GLOBAL_LOG(ERROR,
               "First element of audio path must be an InputTerminal, "
               "but a unit of type \"%s\" was discovered instead!\n",
               last_unit.type_name());
    return ZX_ERR_INTERNAL;
  }

  // Locate and stash a pointer to the terminal which serves as the bridge to
  // the host.  If this is the output terminal, then this path is an audio
  // input to the system, and vice-versa.  There should be exactly one stream
  // terminal in our path.
  //
  // If the stream terminal is an output terminal, then this is an audio input
  // path.  Otherwise it is an audio output path.
  const auto& out_term = static_cast<const OutputTerminal&>(first_unit);
  const auto& in_term = static_cast<const InputTerminal&>(last_unit);
  if (out_term.is_stream_terminal() == in_term.is_stream_terminal()) {
    GLOBAL_LOG(ERROR, "%s stream terminals found in audio path!\n",
               out_term.is_stream_terminal() ? "Multiple" : "No");
    return ZX_ERR_INTERNAL;
  }

  if (out_term.is_stream_terminal()) {
    stream_terminal_.reset(&out_term);
    direction_ = Direction::Input;
  } else {
    stream_terminal_.reset(&in_term);
    direction_ = Direction::Output;
  }

  // Now walk the array of AudioUnits configuring our path.  In particular...
  //
  // ++ If we find SelectorUnits, make sure that they are configured to select
  //    the input which comes immediately before them.
  // ++ If we find MixerUnits, make sure that they are configured to pass
  //    through audio from the input which comes immediately before them.
  // ++ If we find FeatureUnits, make sure to stash a pointer to the first one
  //    we find.  This is where our volume control knob will be located (if
  //    any).
  //
  //    If any mixers or selectors we encounter are already in use, abort.  We
  //    don't know how to properly configure a device where multiple paths
  //    exist which share mixer/selector units.
  for (uint32_t i = 1; i < unit_count_ - 1; ++i) {
    const auto& unit = units_[i];

    // Skip anything which is not a selector, mixer, or feature unit.
    switch (unit->type()) {
      case AudioUnit::Type::SelectorUnit:
      case AudioUnit::Type::MixerUnit:
      case AudioUnit::Type::FeatureUnit:
        break;
      default:
        continue;
    }

    // Make sure the unit is not already in use.  We don't know how to share
    // any of these units with other paths.
    if (unit->in_use()) {
      GLOBAL_LOG(ERROR,
                 "AudioPath with in/out term ids = (%u/%u) encountered a %s "
                 "(id %u) which is already in use by another path.\n",
                 in_term.id(), out_term.id(), unit->type_name(), unit->id());
      return ZX_ERR_NOT_SUPPORTED;
    }

    if (unit->type() == AudioUnit::Type::SelectorUnit) {
      // Make certain that the upstream unit for this audio path is the
      // unit which has been selected.
      auto& selector_unit = static_cast<SelectorUnit&>(*unit);
      uint8_t upstream_id = static_cast<uint8_t>(units_[i + 1]->id());
      zx_status_t status = selector_unit.Select(proto, upstream_id);
      if (status != ZX_OK) {
        GLOBAL_LOG(ERROR,
                   "AudioPath with in/out term ids = (%u/%u) failed to set "
                   "selector id %u to source from upstream unit id %u (status %d)\n",
                   in_term.id(), out_term.id(), unit->id(), upstream_id, status);
        return status;
      }
    } else if (unit->type() == AudioUnit::Type::MixerUnit) {
      // TODO(johngro): (configure the mixer here)
    } else if (unit->type() == AudioUnit::Type::FeatureUnit) {
      // Right now, we don't know how to deal with a path which has
      // multiple volume knobs.
      if (feature_unit_ != nullptr) {
        GLOBAL_LOG(ERROR,
                   "AudioPath with in/out term ids = (%u/%u) encountered "
                   "a multiple feature units in the path.  We encountered "
                   "id %u, but already have id %u cached.\n",
                   in_term.id(), out_term.id(), unit->id(), feature_unit_->id());
        return ZX_ERR_NOT_SUPPORTED;
      }

      feature_unit_ = fbl::RefPtr<FeatureUnit>::Downcast(unit);
    }
  }

  // Things look good.  Flag all of the units in our path as being in use now.
  for (uint32_t i = 1; i < unit_count_ - 1; ++i) {
    units_[i]->set_in_use();
  }

  // If this path has a feature unit, then default the volume controls to 0dB
  // gain and unmuted.
  if (feature_unit_ != nullptr) {
    feature_unit_->SetMute(proto, false);
    feature_unit_->SetVol(proto, 0.0f);
    feature_unit_->SetAgc(proto, false);
  }

  return ZX_OK;
}

}  // namespace usb
}  // namespace audio
