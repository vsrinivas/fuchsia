// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_AUDIO_UNITS_H_
#define SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_AUDIO_UNITS_H_

#include <zircon/hw/usb/audio.h>

#include <memory>
#include <utility>

#include <fbl/array.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "usb-audio-descriptors.h"

// Notes: usb-audio-units.h contains a collection of definitions of classes used
// when building the graph of Terminals/Units which make up the inside of a USB
// Audio Control interface.

namespace audio {
namespace usb {

class AudioUnit : public fbl::WAVLTreeContainable<fbl::RefPtr<AudioUnit>>,
                  public fbl::RefCounted<AudioUnit> {
 public:
  static constexpr uint32_t kInvalidID = 0xFFFFFFFF;

  // clang-format off
    enum class Type : uint8_t {
        InputTerminal  = USB_AUDIO_AC_INPUT_TERMINAL,
        OutputTerminal = USB_AUDIO_AC_OUTPUT_TERMINAL,
        MixerUnit      = USB_AUDIO_AC_MIXER_UNIT,
        SelectorUnit   = USB_AUDIO_AC_SELECTOR_UNIT,
        FeatureUnit    = USB_AUDIO_AC_FEATURE_UNIT,
        ProcessingUnit = USB_AUDIO_AC_PROCESSING_UNIT,
        ExtensionUnit  = USB_AUDIO_AC_EXTENSION_UNIT,
    };
  // clang-format on

  static fbl::RefPtr<AudioUnit> Create(const DescriptorListMemory::Iterator& iter, uint8_t iid);

  Type type() const { return static_cast<Type>(desc_->bDescriptorSubtype); }
  const char* type_name() const;
  uint8_t iid() const { return iid_; }
  uint32_t id() const { return desc_->bID; }
  uint32_t GetKey() const { return id(); }

  // the 16 bit index which needs to be used any time a command needs to be
  // sent to this unit (the wIndex field).  This is formed from the unit ID
  // (high byte) and the control interface id (low byte).
  uint16_t index() const { return static_cast<uint16_t>((id() << 8) | iid()); }

  // Every audio unit needs to define which source(s) feed it.  This
  // information is contained in the unit/terminal's descriptors, but where it
  // lives (and whether or not it is simply implied, such as in the case of an
  // InputTerminal) depends entirely on the type of unit/terminal in question.
  // Because of this, we simply model the interface using pure virtual
  // methods.
  virtual uint32_t source_count() const = 0;
  virtual uint32_t source_id(uint32_t ndx) const = 0;

  // A hook which allows certain audio units/terminals to read their
  // capabilities at startup.  Not all of the units need to do this, so the
  // default hook implementation is a no-op.
  virtual zx_status_t Probe(const usb_protocol_t& proto) { return ZX_OK; }

  // A state flag used by the audio control interface class when it is
  // searching the terminal/unit graph for audio paths to publish.
  bool& visited() { return visited_; }

  // A flag indicating whether or not there is at least one audio path in the
  // system attempting to use this unit/terminal
  bool in_use() const { return in_use_; }
  void set_in_use() { in_use_ = true; }

 protected:
  friend class fbl::RefPtr<AudioUnit>;

  AudioUnit(fbl::RefPtr<DescriptorListMemory> desc_list, const usb_audio_ac_ut_desc* desc,
            uint8_t iid)
      : desc_list_(std::move(desc_list)), desc_(desc), iid_(iid) {}
  virtual ~AudioUnit() {}

  DISALLOW_COPY_ASSIGN_AND_MOVE(AudioUnit);

  template <typename T>
  zx_status_t CtrlReq(const usb_protocol_t& proto, uint8_t code, uint16_t val, T* data) {
    return CtrlReq(proto, code, val, sizeof(*data), data);
  }

  // See note in UsbAudioControlInterface.  Holding a constant RefPtr to our
  // descriptor list ensures that we can never accidentally release the list
  // while we still exist.
  const fbl::RefPtr<DescriptorListMemory> desc_list_;
  const usb_audio_ac_ut_desc* const desc_;

  // The interface id of the control interface that this unit/terminal belongs
  // to.  All units need to know this number in order to properly address
  // get/set commands.
  const uint8_t iid_;

 private:
  zx_status_t CtrlReq(const usb_protocol_t& proto, uint8_t code, uint16_t val, uint16_t len,
                      void* data);

  // State flags used when building valid audio paths.
  bool visited_ = false;
  bool in_use_ = false;
};

class Terminal : public AudioUnit {
 public:
  uint16_t terminal_type() const { return term_desc_->wTerminalType; }
  bool is_stream_terminal() const { return terminal_type() == USB_AUDIO_TERMINAL_USB_STREAMING; }
  bool is_usb_terminal() const {
    // See Universal Serial Bus Device Class Definition for Terminal Types,
    // rev 1.0 Section 2.1
    return (terminal_type() & 0xFF00) == 0x0100;
  }

 protected:
  Terminal(fbl::RefPtr<DescriptorListMemory> desc_list, const usb_audio_ac_terminal_desc* desc,
           uint8_t iid)
      : AudioUnit(std::move(desc_list), reinterpret_cast<const usb_audio_ac_ut_desc*>(desc), iid),
        term_desc_(desc) {}

  DISALLOW_COPY_ASSIGN_AND_MOVE(Terminal);

 private:
  const usb_audio_ac_terminal_desc* const term_desc_;
};

class InputTerminal : public Terminal {
 public:
  uint32_t source_count() const final { return 0; }
  uint32_t source_id(uint32_t ndx) const final { return kInvalidID; }

 private:
  friend class AudioUnit;

  static fbl::RefPtr<InputTerminal> Create(const DescriptorListMemory::Iterator& iter, uint8_t iid);

  InputTerminal(fbl::RefPtr<DescriptorListMemory> desc_list,
                const usb_audio_ac_input_terminal_desc* desc, uint8_t iid)
      : Terminal(std::move(desc_list), reinterpret_cast<const usb_audio_ac_terminal_desc*>(desc),
                 iid) {}

  DISALLOW_COPY_ASSIGN_AND_MOVE(InputTerminal);

  const usb_audio_ac_input_terminal_desc* input_desc() const {
    return reinterpret_cast<const usb_audio_ac_input_terminal_desc*>(desc_);
  }
};

class OutputTerminal : public Terminal {
 public:
  uint32_t source_count() const final { return 1; }
  uint32_t source_id(uint32_t ndx) const final {
    return (ndx == 0) ? output_desc()->bSourceID : kInvalidID;
  }

 private:
  friend class AudioUnit;

  static fbl::RefPtr<OutputTerminal> Create(const DescriptorListMemory::Iterator& iter,
                                            uint8_t iid);

  OutputTerminal(fbl::RefPtr<DescriptorListMemory> desc_list,
                 const usb_audio_ac_output_terminal_desc* desc, uint8_t iid)
      : Terminal(std::move(desc_list), reinterpret_cast<const usb_audio_ac_terminal_desc*>(desc),
                 iid) {}

  DISALLOW_COPY_ASSIGN_AND_MOVE(OutputTerminal);

  const usb_audio_ac_output_terminal_desc* output_desc() const {
    return reinterpret_cast<const usb_audio_ac_output_terminal_desc*>(desc_);
  }
};

class MixerUnit : public AudioUnit {
 public:
  uint32_t source_count() const final { return mixer_desc()->bNrInPins; }
  uint32_t source_id(uint32_t ndx) const final {
    return (ndx < source_count()) ? mixer_desc()->baSourceID[ndx] : kInvalidID;
  }

  const usb_audio_ac_mixer_unit_desc_0* mixer_desc() const {
    return reinterpret_cast<const usb_audio_ac_mixer_unit_desc_0*>(desc_);
  }
  const usb_audio_ac_mixer_unit_desc_1* mixer_desc_1() const { return mixer_desc_1_; }
  const usb_audio_ac_mixer_unit_desc_2* mixer_desc_2() const { return mixer_desc_2_; }

  // TODO(johngro): Add a probe method to mixer so that we can read all of the
  // mix/max/cur settings for the mixer crossbar.  Because of the way that we
  // are organizing our graph, this method may need to be extended to have
  // access to the set of all units present in the control interface.

 private:
  friend class AudioUnit;

  static fbl::RefPtr<MixerUnit> Create(const DescriptorListMemory::Iterator& iter, uint8_t iid);

  MixerUnit(fbl::RefPtr<DescriptorListMemory> desc_list,
            const usb_audio_ac_mixer_unit_desc_0* desc_0,
            const usb_audio_ac_mixer_unit_desc_1* desc_1,
            const usb_audio_ac_mixer_unit_desc_2* desc_2, uint8_t iid)
      : AudioUnit(std::move(desc_list), reinterpret_cast<const usb_audio_ac_ut_desc*>(desc_0), iid),
        mixer_desc_1_(desc_1),
        mixer_desc_2_(desc_2) {}

  DISALLOW_COPY_ASSIGN_AND_MOVE(MixerUnit);

  const usb_audio_ac_mixer_unit_desc_1* const mixer_desc_1_;
  const usb_audio_ac_mixer_unit_desc_2* const mixer_desc_2_;
};

class SelectorUnit : public AudioUnit {
 public:
  uint32_t source_count() const final { return selector_desc()->bNrInPins; }
  uint32_t source_id(uint32_t ndx) const final {
    return (ndx < source_count()) ? selector_desc()->baSourceID[ndx] : kInvalidID;
  }

  const usb_audio_ac_selector_unit_desc_0* selector_desc() const {
    return reinterpret_cast<const usb_audio_ac_selector_unit_desc_0*>(desc_);
  }

  const usb_audio_ac_selector_unit_desc_1* selector_desc_1() const { return selector_desc_1_; }

  // Select the input to the selector unit identified by the desired upstream
  // unit's id;
  zx_status_t Select(const usb_protocol_t& proto, uint8_t upstream_id);

 private:
  friend class AudioUnit;

  static fbl::RefPtr<SelectorUnit> Create(const DescriptorListMemory::Iterator& iter, uint8_t iid);

  SelectorUnit(fbl::RefPtr<DescriptorListMemory> desc_list,
               const usb_audio_ac_selector_unit_desc_0* desc_0,
               const usb_audio_ac_selector_unit_desc_1* desc_1, uint8_t iid)
      : AudioUnit(std::move(desc_list), reinterpret_cast<const usb_audio_ac_ut_desc*>(desc_0), iid),
        selector_desc_1_(desc_1) {}

  DISALLOW_COPY_ASSIGN_AND_MOVE(SelectorUnit);

  const usb_audio_ac_selector_unit_desc_1* const selector_desc_1_;
};

class FeatureUnit : public AudioUnit {
 public:
  uint32_t source_count() const final { return 1; }
  uint32_t source_id(uint32_t ndx) const final { return feature_desc()->bSourceID; }

  bool has_vol() const { return (master_feat_ | ch_feat_) & USB_AUDIO_FU_BMA_VOLUME; }
  bool has_agc() const { return (master_feat_ | ch_feat_) & USB_AUDIO_FU_BMA_AUTOMATIC_GAIN; }
  bool has_mute() const { return (master_feat_ | ch_feat_) & USB_AUDIO_FU_BMA_MUTE; }

  float vol_min_db() const { return static_cast<float>(vol_min_) * kDbPerTick; }
  float vol_max_db() const { return static_cast<float>(vol_max_) * kDbPerTick; }
  float vol_res_db() const { return static_cast<float>(vol_res_) * kDbPerTick; }
  float vol_cur_db() const { return static_cast<float>(vol_cur_) * kDbPerTick; }
  bool mute_cur() const { return !!mute_cur_; }
  bool agc_cur() const { return !!agc_cur_; }

  const usb_audio_ac_feature_unit_desc_0* feature_desc() const {
    return reinterpret_cast<const usb_audio_ac_feature_unit_desc_0*>(desc_);
  }
  const usb_audio_ac_feature_unit_desc_1* feature_desc_1() const { return feature_desc_1_; }

  zx_status_t Probe(const usb_protocol_t& proto) final;

  // Do the best we can to set the volume/mute/agc.  Return the value actually set.
  float SetVol(const usb_protocol_t& proto, float db);
  bool SetMute(const usb_protocol_t& proto, bool mute);
  bool SetAgc(const usb_protocol_t& proto, bool enabled);

 private:
  friend class AudioUnit;

  // Section 5.2.2.4.3.2 of the USB Audio 1.0 spec
  static constexpr float kDbPerTick = 1.0f / 256.0f;

  // A small struct used to track the various features supported by a channel
  // controlled by this feature unit.
  struct Features {
    bool has_vol() const { return supported_ & USB_AUDIO_FU_BMA_VOLUME; }
    bool has_mute() const { return supported_ & USB_AUDIO_FU_BMA_MUTE; }
    bool has_agc() const { return supported_ & USB_AUDIO_FU_BMA_AUTOMATIC_GAIN; }

    uint32_t supported_ = 0;
    int16_t vol_min_ = 0;
    int16_t vol_max_ = 0;
    int16_t vol_res_ = 0;
    int16_t vol_cur_ = 0;
    bool mute_cur = false;
  };

  static fbl::RefPtr<FeatureUnit> Create(const DescriptorListMemory::Iterator& iter, uint8_t iid);

  // Map a feature ordinal to its corresponding bit in the bmaControls
  // bitmask.  Thankfully, as of the USB Audio 1.0 spec, this is just a simple
  // offset and shift operation.
  static constexpr uint32_t FeatureToBit(uint8_t ord) { return 1u << (ord - 1); }

  FeatureUnit(fbl::RefPtr<DescriptorListMemory> desc_list,
              const usb_audio_ac_feature_unit_desc_0* desc_0,
              const usb_audio_ac_feature_unit_desc_1* desc_1,
              std::unique_ptr<Features[]> feature_mem, size_t feature_len, uint8_t iid)
      : AudioUnit(std::move(desc_list), reinterpret_cast<const usb_audio_ac_ut_desc*>(desc_0), iid),
        feature_desc_1_(desc_1),
        features_(feature_mem.release(), feature_len) {}

  DISALLOW_COPY_ASSIGN_AND_MOVE(FeatureUnit);

  template <typename T>
  zx_status_t FeatCtrlReq(const usb_protocol_t& proto, uint8_t code, uint8_t ctrl, uint8_t ch,
                          T* data) {
    // See Section 5.2.2.4 in the USB Audio 1.0 spec for the encoding of val.
    uint16_t val = static_cast<uint16_t>((static_cast<uint16_t>(ctrl) << 8) | ch);
    return CtrlReq<T>(proto, code, val, data);
  }

  template <typename T>
  void SetFeature(const usb_protocol_t& proto, uint8_t feature, T val) {
    auto mask = FeatureToBit(feature);

    if (master_feat_ & mask) {
      FeatCtrlReq(proto, USB_AUDIO_SET_CUR, feature, 0, &val);
    } else {
      for (size_t i = 1; i < features_.size(); ++i) {
        uint8_t ch = static_cast<uint8_t>(i);
        FeatCtrlReq(proto, USB_AUDIO_SET_CUR, feature, ch, &val);
      }
    }
  }

  const usb_audio_ac_feature_unit_desc_1* const feature_desc_1_;
  const fbl::Array<Features> features_;

  uint32_t master_feat_ = 0;
  uint32_t ch_feat_ = 0;
  int16_t vol_min_ = 0;
  int16_t vol_max_ = 0;
  int16_t vol_res_ = 0;
  int16_t vol_cur_ = 0;
  uint8_t mute_cur_ = 0;
  uint8_t agc_cur_ = 0;
};

class ProcessingUnit : public AudioUnit {
 public:
  uint32_t source_count() const final { return processing_desc()->bNrInPins; }
  uint32_t source_id(uint32_t ndx) const final {
    return (ndx < source_count()) ? processing_desc()->baSourceID[ndx] : kInvalidID;
  }

  const usb_audio_ac_processing_unit_desc_0* processing_desc() const {
    return reinterpret_cast<const usb_audio_ac_processing_unit_desc_0*>(desc_);
  }

  const usb_audio_ac_processing_unit_desc_1* processing_desc_1() const {
    return processing_desc_1_;
  }

  const usb_audio_ac_processing_unit_desc_2* processing_desc_2() const {
    return processing_desc_2_;
  }

 private:
  friend class AudioUnit;

  static fbl::RefPtr<ProcessingUnit> Create(const DescriptorListMemory::Iterator& iter,
                                            uint8_t iid);

  ProcessingUnit(fbl::RefPtr<DescriptorListMemory> desc_list,
                 const usb_audio_ac_processing_unit_desc_0* desc_0,
                 const usb_audio_ac_processing_unit_desc_1* desc_1,
                 const usb_audio_ac_processing_unit_desc_2* desc_2, uint8_t iid)
      : AudioUnit(std::move(desc_list), reinterpret_cast<const usb_audio_ac_ut_desc*>(desc_0), iid),
        processing_desc_1_(desc_1),
        processing_desc_2_(desc_2) {}

  DISALLOW_COPY_ASSIGN_AND_MOVE(ProcessingUnit);

  const usb_audio_ac_processing_unit_desc_1* const processing_desc_1_;
  const usb_audio_ac_processing_unit_desc_2* const processing_desc_2_;
};

class ExtensionUnit : public AudioUnit {
 public:
  uint32_t source_count() const final { return extension_desc()->bNrInPins; }
  uint32_t source_id(uint32_t ndx) const final {
    return (ndx < source_count()) ? extension_desc()->baSourceID[ndx] : kInvalidID;
  }

  const usb_audio_ac_extension_unit_desc_0* extension_desc() const {
    return reinterpret_cast<const usb_audio_ac_extension_unit_desc_0*>(desc_);
  }

  const usb_audio_ac_extension_unit_desc_1* extension_desc_1() const { return extension_desc_1_; }
  const usb_audio_ac_extension_unit_desc_2* extension_desc_2() const { return extension_desc_2_; }

 private:
  friend class AudioUnit;

  static fbl::RefPtr<ExtensionUnit> Create(const DescriptorListMemory::Iterator& iter, uint8_t iid);

  ExtensionUnit(fbl::RefPtr<DescriptorListMemory> desc_list,
                const usb_audio_ac_extension_unit_desc_0* desc_0,
                const usb_audio_ac_extension_unit_desc_1* desc_1,
                const usb_audio_ac_extension_unit_desc_2* desc_2, uint8_t iid)
      : AudioUnit(std::move(desc_list), reinterpret_cast<const usb_audio_ac_ut_desc*>(desc_0), iid),
        extension_desc_1_(desc_1),
        extension_desc_2_(desc_2) {}

  DISALLOW_COPY_ASSIGN_AND_MOVE(ExtensionUnit);

  const usb_audio_ac_extension_unit_desc_1* const extension_desc_1_;
  const usb_audio_ac_extension_unit_desc_2* const extension_desc_2_;
};

}  // namespace usb
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_AUDIO_UNITS_H_
