// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-audio-units.h"

#include <math.h>

#include <limits>
#include <memory>
#include <utility>

#include <fbl/algorithm.h>

#include "debug-logging.h"

namespace audio {
namespace usb {

// a small internal helper methods which handles a bunch of ugly casting for us.
template <typename T, typename U>
static inline const T* offset_ptr(const U* p, size_t offset) {
  return ((offset + sizeof(T)) <= p->bLength)
             ? reinterpret_cast<const T*>(reinterpret_cast<uintptr_t>(p) + offset)
             : nullptr;
}

const char* AudioUnit::type_name() const {
  switch (type()) {
    case Type::InputTerminal:
      return "InputTerminal";
    case Type::OutputTerminal:
      return "OutputTerminal";
    case Type::MixerUnit:
      return "MixerUnit";
    case Type::SelectorUnit:
      return "SelectorUnit";
    case Type::FeatureUnit:
      return "FeatureUnit";
    case Type::ProcessingUnit:
      return "ProcessingUnit";
    case Type::ExtensionUnit:
      return "ExtensionUnit";
    default:
      return "<Unknown>";
  }
}

fbl::RefPtr<AudioUnit> AudioUnit::Create(const DescriptorListMemory::Iterator& iter, uint8_t iid) {
  auto hdr = iter.hdr_as<usb_audio_desc_header>();

  // This should already have been verified by the code calling us.
  ZX_DEBUG_ASSERT(hdr != nullptr);

  switch (hdr->bDescriptorSubtype) {
    case USB_AUDIO_AC_INPUT_TERMINAL:
      return InputTerminal::Create(iter, iid);
    case USB_AUDIO_AC_OUTPUT_TERMINAL:
      return OutputTerminal::Create(iter, iid);
    case USB_AUDIO_AC_MIXER_UNIT:
      return MixerUnit::Create(iter, iid);
    case USB_AUDIO_AC_SELECTOR_UNIT:
      return SelectorUnit::Create(iter, iid);
    case USB_AUDIO_AC_FEATURE_UNIT:
      return FeatureUnit::Create(iter, iid);
    case USB_AUDIO_AC_PROCESSING_UNIT:
      return ProcessingUnit::Create(iter, iid);
    case USB_AUDIO_AC_EXTENSION_UNIT:
      return ExtensionUnit::Create(iter, iid);
    default:
      GLOBAL_LOG(WARN, "Unrecognized audio control descriptor (type %u) @ offset %zu\n",
                 hdr->bDescriptorSubtype, iter.offset());
      return nullptr;
  }
}

zx_status_t AudioUnit::CtrlReq(const usb_protocol_t& proto, uint8_t code, uint16_t val,
                               uint16_t len, void* data) {
  if (!len || (data == nullptr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // For audio class specific control codes, get control codes all have their MSB set.
  uint8_t req_type = (code & 0x80) ? USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE
                                   : USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;

  // TODO(johngro) : See about fixing the use of const in the C API for the
  // USB bus protocol.  There is no good reason why a usb_protocol structure
  // should need to be mutable when performing operations such as usb_control.
  auto proto_ptr = const_cast<usb_protocol_t*>(&proto);

  // TODO(johngro) : Do better than this if we can.
  //
  // None of these control transactions should every take any
  // significant amount of time, and if the turn out to do so, then we really
  // need to find a way to use the USB bus driver in an asynchronous fashion.
  // Even 500 mSec is just *way* too long to ever block a driver thread, for
  // pretty much any reason.  Right now, this timeout is here only for safety
  // reasons; it would be better to timeout after a half of a second then to
  // block the entire USB device forever.
  //
  // It is tempting to simply kill the driver/process if we ever timeout on
  // one of these operations, but at time this code was written, that would
  // kill the entire USB bus driver.  So, for now, we eat the timeout and rely
  // on the code above us taking some action to shut this device down.
  constexpr uint64_t kRelativeTimeout = ZX_MSEC(500);
  size_t done = 0;
  zx_status_t status;
  if ((req_type & USB_DIR_MASK) == USB_DIR_OUT) {
    status = usb_control_out(proto_ptr, req_type, code, val, index(),
                             zx_deadline_after(kRelativeTimeout), data, len);
    done = len;
  } else {
    status = usb_control_in(proto_ptr, req_type, code, val, index(),
                            zx_deadline_after(kRelativeTimeout), data, len, &done);
  }
  if ((status == ZX_OK) && (done != len)) {
    status = ZX_ERR_BUFFER_TOO_SMALL;
  }

  if (status != ZX_OK) {
    GLOBAL_LOG(WARN,
               "WARNING: Audio control request failed! Unit (%s:id %u), "
               "code 0x%02x val 0x%04hx, ndx 0x%04x [bytes expected %u, got %zu] (status %d)\n",
               type_name(), id(), code, val, index(), len, done, status);
  }

  return status;
}

fbl::RefPtr<InputTerminal> InputTerminal::Create(const DescriptorListMemory::Iterator& iter,
                                                 uint8_t iid) {
  auto hdr = iter.hdr_as<usb_audio_ac_input_terminal_desc>();

  if (hdr == nullptr) {
    GLOBAL_LOG(WARN, "InputTerminal header appears invalid @ offset %zu\n", iter.offset());
    return nullptr;
  }

  // TODO(johngro): additional sanity checking and pre-processing goes here.

  fbl::AllocChecker ac;
  auto ret = fbl::AdoptRef(new (&ac) InputTerminal(iter.desc_list(), hdr, iid));
  return ac.check() ? ret : nullptr;
}

fbl::RefPtr<OutputTerminal> OutputTerminal::Create(const DescriptorListMemory::Iterator& iter,
                                                   uint8_t iid) {
  auto hdr = iter.hdr_as<usb_audio_ac_output_terminal_desc>();

  if (hdr == nullptr) {
    GLOBAL_LOG(WARN, "OutputTerminal header appears invalid @ offset %zu\n", iter.offset());
    return nullptr;
  }

  // TODO(johngro): additional sanity checking and pre-processing goes here.

  fbl::AllocChecker ac;
  auto ret = fbl::AdoptRef(new (&ac) OutputTerminal(iter.desc_list(), hdr, iid));
  return ac.check() ? ret : nullptr;
}

fbl::RefPtr<MixerUnit> MixerUnit::Create(const DescriptorListMemory::Iterator& iter, uint8_t iid) {
  // Find the size of each of the inlined variable length arrays in this
  // structure, finding the locations of the constant headers in the process.
  // If anything does not look right, complain and move on.
  auto hdr0 = iter.hdr_as<usb_audio_ac_mixer_unit_desc_0>();
  if (hdr0 != nullptr) {
    size_t off = sizeof(*hdr0) + hdr0->bNrInPins;
    auto hdr1 = offset_ptr<usb_audio_ac_mixer_unit_desc_1>(hdr0, off);
    if (hdr1 != nullptr) {
      // Determining the size of bmControls is a bit of a pain.  To do so,
      // we need to know 'n', which is the sum the number of channels
      // across all of the input pins, and 'm' (which should be
      // hdr1->bNrChannels).  At this stage of parsing our unit/terminal
      // graph, we may not have access to all of the sources which might
      // feed into the calculation of 'n'.  Because of this, for now, just
      // assume that the size of bmControls (in bytes) is equal to the
      // space remaining in the descriptor, demanding that this be at
      // least equal to a single byte (if it was zero, it means that we
      // either have no input or no output channels, neither of which
      // makes sense).
      if (sizeof(usb_audio_ac_mixer_unit_desc_2) < hdr0->bLength) {
        size_t off2 = hdr0->bLength - sizeof(usb_audio_ac_mixer_unit_desc_2);
        if (off2 > off) {
          auto hdr2 = offset_ptr<usb_audio_ac_mixer_unit_desc_2>(hdr0, off2);
          ZX_DEBUG_ASSERT(hdr2 != nullptr);

          // TODO(johngro): additional sanity checking and pre-processing goes here.
          fbl::AllocChecker ac;
          auto ret = fbl::AdoptRef(new (&ac) MixerUnit(iter.desc_list(), hdr0, hdr1, hdr2, iid));
          return ac.check() ? ret : nullptr;
        }
      }
    }
  }

  GLOBAL_LOG(WARN, "MixerUnit header appears invalid @ offset %zu\n", iter.offset());
  return nullptr;
}

fbl::RefPtr<SelectorUnit> SelectorUnit::Create(const DescriptorListMemory::Iterator& iter,
                                               uint8_t iid) {
  // Find the size of each of the inlined variable length arrays in this
  // structure, finding the locations of the constant headers in the process.
  // If anything does not look right, complain and move on.
  auto hdr0 = iter.hdr_as<usb_audio_ac_selector_unit_desc_0>();
  if (hdr0 != nullptr) {
    size_t off = sizeof(*hdr0) + hdr0->bNrInPins;
    auto hdr1 = offset_ptr<usb_audio_ac_selector_unit_desc_1>(hdr0, off);
    if (hdr1 != nullptr) {
      // TODO(johngro): additional sanity checking and pre-processing goes here.
      fbl::AllocChecker ac;
      auto ret = fbl::AdoptRef(new (&ac) SelectorUnit(iter.desc_list(), hdr0, hdr1, iid));
      return ac.check() ? ret : nullptr;
    }
  }

  GLOBAL_LOG(WARN, "SelectorUnit header appears invalid @ offset %zu\n", iter.offset());
  return nullptr;
}

zx_status_t SelectorUnit::Select(const usb_protocol_t& proto, uint8_t upstream_id) {
  // Section 5.2.2.3.3. defines the selector index as being 1s indexed, so
  // zero is an easy to use "invalid" value.
  uint8_t ndx = 0;

  // Find the appropriate index or return an error trying.
  uint32_t cnt = source_count();
  for (uint32_t i = 0; i < cnt; ++i) {
    if (upstream_id == source_id(i)) {
      ndx = static_cast<uint8_t>(i + 1);
      break;
    }
  }

  if (!ndx) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Now go ahead and set the value;
  return CtrlReq(proto, USB_AUDIO_SET_CUR, 0, &ndx);
}

fbl::RefPtr<FeatureUnit> FeatureUnit::Create(const DescriptorListMemory::Iterator& iter,
                                             uint8_t iid) {
  // Find the size of each of the inlined variable length arrays in this
  // structure, finding the locations of the constant headers in the process.
  // If anything does not look right, complain and move on.
  auto hdr0 = iter.hdr_as<usb_audio_ac_feature_unit_desc_0>();
  if (hdr0 != nullptr) {
    // The exact expected size of the Controls bitmap depends on the number
    // of channels feeding this feature unit.  This information is not
    // contained in the feature unit itself, but instead exists upstream of
    // the unit in first unit/terminal which contains a channel cluster
    // element.  At this point in parsing, we have not discovered all of the
    // units present in the audio control interface yet, so we cannot trace
    // upstream to sanity check the size of this field.
    //
    // For now, we perform the most basic check we can by assuming that the
    // size of the Controls bitmap must be...
    //
    // 1) Non-zero, and...
    // 2) Divisible by bControlSize, which must also be non-zero.
    //
    // In the future, more stringent checks can be applied during Probe.
    constexpr size_t kHdrOverhead = sizeof(*hdr0) + sizeof(usb_audio_ac_feature_unit_desc_1);
    size_t ctrl_array_bytes = hdr0->bLength - kHdrOverhead;
    if ((kHdrOverhead < hdr0->bLength) && (hdr0->bControlSize > 0) &&
        (!(ctrl_array_bytes % hdr0->bControlSize))) {
      // Allocate memory for our Features capability array.
      fbl::AllocChecker ac;
      size_t feat_len = ctrl_array_bytes / hdr0->bControlSize;
      auto feat_mem = std::unique_ptr<Features[]>(new (&ac) Features[feat_len]);

      if (ac.check()) {
        // We just made sure that this fits, there should be no way for us
        // to have run out of data.
        size_t off = hdr0->bLength - sizeof(usb_audio_ac_feature_unit_desc_1);
        auto hdr1 = offset_ptr<usb_audio_ac_feature_unit_desc_1>(hdr0, off);
        ZX_DEBUG_ASSERT(hdr1 != nullptr);

        auto ret = fbl::AdoptRef(new (&ac) FeatureUnit(iter.desc_list(), hdr0, hdr1,
                                                       std::move(feat_mem), feat_len, iid));
        if (ac.check()) {
          return ret;
        }
      }

      GLOBAL_LOG(WARN, "Out of memory attempting to allocate FeatureUnit @ offset %zu\n",
                 iter.offset());
      return nullptr;
    }
  }

  GLOBAL_LOG(WARN, "FeatureUnit header appears invalid @ offset %zu\n", iter.offset());
  return nullptr;
}

zx_status_t FeatureUnit::Probe(const usb_protocol_t& proto) {
  zx_status_t res;

  // Start by going over our channel feature bitmap and extracting the actual
  // feature bits for each channel.  Right now, we demand that the size of
  // each entry be (at most) a 32 bit integer.  The USB Audio 1.0 Spec only
  // defines bits up to bit 9, so we really only understand how to handle up
  // to there.  If we cannot fit each of the bitmap entries in a 32-bit
  // integer, then the USB audio spec has come a long way and someone should
  // come back here and update this driver.
  ZX_DEBUG_ASSERT(feature_desc()->bControlSize != 0);  // Create should have checked this already
  if (feature_desc()->bControlSize > sizeof(uint32_t)) {
    GLOBAL_LOG(WARN, "FeatureUnit id %u has unsupported bControlSize > %zu (%u)\n", id(),
               sizeof(uint32_t), feature_desc()->bControlSize);
    return ZX_ERR_NOT_SUPPORTED;
  }

  for (size_t i = 0; i < features_.size(); ++i) {
    auto& f = features_[i];
    f.supported_ = 0;
    for (uint8_t j = 0; j < feature_desc()->bControlSize; ++j) {
      uint32_t bits = feature_desc()->bmaControls[(i * feature_desc()->bControlSize) + j];
      f.supported_ |= bits << (8 * j);
    }
  }

  // Now, go over our array of features and compute both the union and the
  // intersection of the features for all of the individual channels.
  uint32_t ch_feat_union = 0;
  uint32_t ch_feat_intersection = 0;
  if (features_.size() > 1) {
    ch_feat_union = features_[1].supported_;
    ch_feat_intersection = features_[1].supported_;
    for (size_t i = 2; i < features_.size(); ++i) {
      ch_feat_union |= features_[i].supported_;
      ch_feat_intersection &= features_[i].supported_;
    }
  }

  // Next check for a set of uniformity requirements.  In particular, there
  // are three types of controls (mute, AGC, and volume/gain) that we want to
  // enforce these guarantees for.  Specifically,
  //
  // 1) We can handle these controls at the master level, or the individual
  //    channel level, but we don't really know what to do if the controls
  //    exist at both levels.
  // 2) If we are controlling these things at the individual control level, we
  //    are doing so in a way which mimics a master control knob only.  So, if
  //    we have these controls at the per-channel level, it is important that
  //    they be they be identical for each of the individual channels.
  constexpr uint32_t kUniformControls =
      USB_AUDIO_FU_BMA_MUTE | USB_AUDIO_FU_BMA_VOLUME | USB_AUDIO_FU_BMA_AUTOMATIC_GAIN;
  ZX_DEBUG_ASSERT(features_.size() > 0);  // Create should have checked this already
  if (((features_[0].supported_ & ch_feat_union & kUniformControls) != 0) ||  // Check #1
      ((ch_feat_union ^ ch_feat_intersection) & kUniformControls)) {          // Check #2
    GLOBAL_LOG(WARN,
               "FeatureUnit id %u has unsupported non-uniform gain controls.  "
               "Master 0x%08x, Channel Union 0x%08x, Channel Intersection 0x%08x.\n",
               id(), features_[0].supported_, ch_feat_union, ch_feat_intersection);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Stash bitmaps of controls we care about for later.
  master_feat_ = features_[0].supported_ & kUniformControls;
  ch_feat_ = ch_feat_intersection & kUniformControls;

  // If this feature unit has volume control, fetch and sanity check the
  // min/max/res of all of the channels.
  if (has_vol()) {
    // Go over each of the volume controls and cache the min/max/res values.
    for (size_t i = 0; i < features_.size(); ++i) {
      auto& f = features_[i];

      if (!f.has_vol()) {
        continue;
      }

      uint8_t ch = static_cast<uint8_t>(i);

      res = FeatCtrlReq(proto, USB_AUDIO_GET_MIN, USB_AUDIO_VOLUME_CONTROL, ch, &f.vol_min_);
      if (res != ZX_OK) {
        return res;
      }

      res = FeatCtrlReq(proto, USB_AUDIO_GET_MAX, USB_AUDIO_VOLUME_CONTROL, ch, &f.vol_max_);
      if (res != ZX_OK) {
        return res;
      }

      res = FeatCtrlReq(proto, USB_AUDIO_GET_RES, USB_AUDIO_VOLUME_CONTROL, ch, &f.vol_res_);
      if (res != ZX_OK) {
        return res;
      }
    }

    // If volume control is done at the per-channel level, make sure that all of
    // the channels support the same range.  Otherwise, our volume control range
    // is equal to the master channel's range.
    if (features_[0].has_vol()) {
      vol_min_ = features_[0].vol_min_;
      vol_max_ = features_[0].vol_max_;
      vol_res_ = features_[0].vol_res_;
    } else {
      vol_min_ = features_[1].vol_min_;
      vol_max_ = features_[1].vol_max_;
      vol_res_ = features_[1].vol_res_;
      for (size_t i = 2; i < features_.size(); ++i) {
        if ((vol_min_ != features_[i].vol_min_) || (vol_max_ != features_[i].vol_max_) ||
            (vol_res_ != features_[i].vol_res_)) {
          GLOBAL_LOG(WARN,
                     "FeatureUnit id %u has unsupported non-uniform gain controls.  "
                     "Channel %zu's gain range [%hd, %hd, %hd] does not match Channel 1's "
                     "range [%hd, %hd, %hd]\n",
                     id(), i, vol_min_, vol_max_, vol_res_, features_[i].vol_min_,
                     features_[i].vol_max_, features_[i].vol_res_);
          return ZX_ERR_NOT_SUPPORTED;
        }
      }
    }

    if (vol_min_ > vol_max_) {
      GLOBAL_LOG(WARN, "FeatureUnit id %u has invalid volume range [%hd, %hd]\n", id(), vol_min_,
                 vol_max_);
      return ZX_ERR_NOT_SUPPORTED;
    }

    if (!vol_res_) {
      GLOBAL_LOG(WARN, "FeatureUnit id %u has invalid volume res %hd\n", id(), vol_res_);
      return ZX_ERR_NOT_SUPPORTED;
    }

    // Fetch the current volume setting from the appropriate source, then
    // make certain that all channels are set to the same if there is no
    // master control knob.
    bool master_control = (master_feat_ & USB_AUDIO_FU_BMA_VOLUME);
    uint8_t ch = master_control ? 0 : 1;
    res = FeatCtrlReq(proto, USB_AUDIO_GET_CUR, USB_AUDIO_VOLUME_CONTROL, ch, &vol_cur_);
    if (res != ZX_OK) {
      return res;
    }

    if (!master_control) {
      SetFeature(proto, USB_AUDIO_VOLUME_CONTROL, vol_cur_);
    }
  }

  // If we have mute controls, figure out the current setting.
  if (has_mute()) {
    res = FeatCtrlReq(proto, USB_AUDIO_GET_CUR, USB_AUDIO_MUTE_CONTROL, 0, &mute_cur_);
    if (res != ZX_OK) {
      return res;
    }
  }

  // If we have agc controls, figure out the current setting.
  if (has_agc()) {
    res = FeatCtrlReq(proto, USB_AUDIO_GET_CUR, USB_AUDIO_AUTOMATIC_GAIN_CONTROL, 0, &agc_cur_);
    if (res != ZX_OK) {
      return res;
    }
  }

  // Dump some diags info if TRACE level logging is enabled.
  if (has_vol()) {
    GLOBAL_LOG(TRACE, "FeatureUnit id %u: can%s mute, can%s AGC, gain [%.3f, %.3f: step %.3f] dB\n",
               id(), has_mute() ? "" : "not", has_agc() ? "" : "not", vol_min_db(), vol_max_db(),
               vol_res_db());
  } else {
    GLOBAL_LOG(TRACE, "FeatureUnit id %u: can%s mute, can%s AGC, and has fixed gain\n", id(),
               has_mute() ? "" : "not", has_agc() ? "" : "not");
  }

  // All done!  Declare success and get out.
  return ZX_OK;
}

float FeatureUnit::SetVol(const usb_protocol_t& proto, float db) {
  // If we have no volume control, then our gain is fixed at 0.0 dB no matter
  // what the user asks for.
  if (!has_vol()) {
    return 0.0;
  }

  // Convert to our target value.  Start by converting to ticks.
  float ticks_float = db / kDbPerTick;

  // Now snap to the closest allowed tick based on our resolution.
  ticks_float = roundf(ticks_float / vol_res_) * vol_res_;

  // Now clamp to the acceptable min/max range and convert to integer ticks.
  vol_cur_ = static_cast<int16_t>(fbl::clamp<float>(ticks_float, vol_min_, vol_max_));

  // Finally apply the setting.  If we have no explicit mute control, and we
  // are currently supposed to be muted, skip this step.  We are using the
  // volume control to simulate mute to the best of our abilities; we will
  // restore vol_cur_ when the unit finally becomes un-muted.
  if (!(mute_cur_ && !has_mute())) {
    SetFeature(proto, USB_AUDIO_VOLUME_CONTROL, vol_cur_);
  }

  return vol_cur_ * kDbPerTick;
}

bool FeatureUnit::SetMute(const usb_protocol_t& proto, bool mute) {
  mute_cur_ = mute;

  // If we have an explicit mute control, use that.  Otherwise, do the best we
  // can using the volume control (if present).
  if (has_mute()) {
    SetFeature(proto, USB_AUDIO_MUTE_CONTROL, mute_cur_);
  } else {
    // Section 5.2.2.4.3.2 of the USB Audio 1.0 spec defines int16::min as
    // -inf dB for the purpose of setting gain.
    int16_t tgt = mute ? std::numeric_limits<int16_t>::min() : vol_cur_;
    SetFeature(proto, USB_AUDIO_VOLUME_CONTROL, tgt);
  }

  return !!mute_cur_;
}

bool FeatureUnit::SetAgc(const usb_protocol_t& proto, bool agc) {
  if (has_agc()) {
    agc_cur_ = agc;
    SetFeature(proto, USB_AUDIO_AUTOMATIC_GAIN_CONTROL, static_cast<uint8_t>(agc));
  }
  return !!agc_cur_;
}

fbl::RefPtr<ProcessingUnit> ProcessingUnit::Create(const DescriptorListMemory::Iterator& iter,
                                                   uint8_t iid) {
  // Find the size of each of the inlined variable length arrays in this
  // structure, finding the locations of the constant headers in the process.
  // If anything does not look right, complain and move on.
  auto hdr0 = iter.hdr_as<usb_audio_ac_processing_unit_desc_0>();
  if (hdr0 != nullptr) {
    size_t off = sizeof(*hdr0) + hdr0->bNrInPins;
    auto hdr1 = offset_ptr<usb_audio_ac_processing_unit_desc_1>(hdr0, off);
    if (hdr1 != nullptr) {
      off += sizeof(*hdr1) + hdr1->bControlSize;
      auto hdr2 = offset_ptr<usb_audio_ac_processing_unit_desc_2>(hdr0, off);

      // TODO(johngro): additional sanity checking and pre-processing goes here.
      //
      // Note: Processing units actually come in their own pre-defined
      // sub-flavors (determined by hdr0->wProcessType).  Instead of
      // lumping them all together into one ProcessingUnit class, we
      // should probably take the time to break them down into the various
      // sub-flavors, at which point in time, the big validation switch
      // statement would go somewhere in here.
      //
      // For now, however, we do not expect to have any need to control
      // processing units.  If we ever encounter one, we really only want
      // to understand the size of the baSourceID array so that we can
      // successfully walk the graph when attempting to build input/output
      // stream paths.
      fbl::AllocChecker ac;
      auto ret = fbl::AdoptRef(new (&ac) ProcessingUnit(iter.desc_list(), hdr0, hdr1, hdr2, iid));
      return ac.check() ? ret : nullptr;
    }
  }

  GLOBAL_LOG(WARN, "ProcessingUnit header appears invalid @ offset %zu\n", iter.offset());
  return nullptr;
}

fbl::RefPtr<ExtensionUnit> ExtensionUnit::Create(const DescriptorListMemory::Iterator& iter,
                                                 uint8_t iid) {
  // Find the size of each of the inlined variable length arrays in this
  // structure, finding the locations of the constant headers in the process.
  // If anything does not look right, complain and move on.
  auto hdr0 = iter.hdr_as<usb_audio_ac_extension_unit_desc_0>();
  if (hdr0 != nullptr) {
    size_t off = sizeof(*hdr0) + hdr0->bNrInPins;
    auto hdr1 = offset_ptr<usb_audio_ac_extension_unit_desc_1>(hdr0, off);
    if (hdr1 != nullptr) {
      off += sizeof(*hdr1) + hdr1->bControlSize;
      auto hdr2 = offset_ptr<usb_audio_ac_extension_unit_desc_2>(hdr0, off);

      // TODO(johngro): additional sanity checking and pre-processing goes here.
      fbl::AllocChecker ac;
      auto ret = fbl::AdoptRef(new (&ac) ExtensionUnit(iter.desc_list(), hdr0, hdr1, hdr2, iid));
      return ac.check() ? ret : nullptr;
    }
  }

  GLOBAL_LOG(WARN, "ExtensionUnit header appears invalid @ offset %zu\n", iter.offset());
  return nullptr;
}

}  // namespace usb
}  // namespace audio
