// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/audio/platform/generic/mixers/point_sampler.h"

#include <algorithm>
#include <limits>

#include "lib/ftl/logging.h"
#include "apps/media/services/audio/audio_track_impl.h"
#include "apps/media/services/audio/platform/generic/mixers/mixer_utils.h"

namespace mojo {
namespace media {
namespace audio {
namespace mixers {

using utils::ScalerType;

// Point Sample Mixer implementation.
template <size_t DChCount, typename SType, size_t SChCount>
class PointSamplerImpl : public PointSampler {
 public:
  PointSamplerImpl() : PointSampler(0, FRAC_ONE - 1) {}

  bool Mix(int32_t* dst,
           uint32_t dst_frames,
           uint32_t* dst_offset,
           const void* src,
           uint32_t frac_src_frames,
           int32_t* frac_src_offset,
           uint32_t frac_step_size,
           Gain::AScale amplitude_scale,
           bool accumulate) override;

 private:
  template <ScalerType ScaleType, bool DoAccumulate>
  static inline bool Mix(int32_t* dst,
                         uint32_t dst_frames,
                         uint32_t* dst_offset,
                         const void* src,
                         uint32_t frac_src_frames,
                         int32_t* frac_src_offset,
                         uint32_t frac_step_size,
                         Gain::AScale amplitude_scale);
};

template <size_t DChCount, typename SType, size_t SChCount>
template <ScalerType ScaleType, bool DoAccumulate>
inline bool PointSamplerImpl<DChCount, SType, SChCount>::Mix(
    int32_t* dst,
    uint32_t dst_frames,
    uint32_t* dst_offset,
    const void* src_void,
    uint32_t frac_src_frames,
    int32_t* frac_src_offset,
    uint32_t frac_step_size,
    Gain::AScale amplitude_scale) {
  using SR = utils::SrcReader<SType, SChCount, DChCount>;
  using DM = utils::DstMixer<ScaleType, DoAccumulate>;

  const SType* src = static_cast<const SType*>(src_void);
  uint32_t doff = *dst_offset;
  int32_t soff = *frac_src_offset;

  FTL_DCHECK(frac_src_frames <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));
  FTL_DCHECK(soff < static_cast<int32_t>(frac_src_frames));
  FTL_DCHECK(soff >= 0);

  // If we are not attenuated to the point of being muted, go ahead and perform
  // the mix.  Otherwise, just update the source and dest offsets.
  if (ScaleType != ScalerType::MUTED) {
    while ((doff < dst_frames) &&
           (soff < static_cast<int32_t>(frac_src_frames))) {
      uint32_t src_iter;
      int32_t* out;

      src_iter = (soff >> AudioTrackImpl::PTS_FRACTIONAL_BITS) * SChCount;
      out = dst + (doff * DChCount);

      for (size_t dst_iter = 0; dst_iter < DChCount; ++dst_iter) {
        int32_t sample = SR::Read(src + src_iter + (dst_iter / SR::DstPerSrc));
        out[dst_iter] = DM::Mix(out[dst_iter], sample, amplitude_scale);
      }

      doff += 1;
      soff += frac_step_size;
    }
  } else {
    if (doff < dst_frames) {
      // Figure out how many samples we would have produced and update the soff
      // and doff values appropriately.
      uint32_t src_avail =
          ((frac_src_frames - soff) + frac_step_size - 1) / frac_step_size;
      uint32_t dst_avail = (dst_frames - doff);
      uint32_t avail = std::min(src_avail, dst_avail);

      soff += avail * frac_step_size;
      doff += avail;
    }
  }

  *dst_offset = doff;
  *frac_src_offset = soff;

  return (soff >= static_cast<int32_t>(frac_src_frames));
}

template <size_t DChCount, typename SType, size_t SChCount>
bool PointSamplerImpl<DChCount, SType, SChCount>::Mix(
    int32_t* dst,
    uint32_t dst_frames,
    uint32_t* dst_offset,
    const void* src,
    uint32_t frac_src_frames,
    int32_t* frac_src_offset,
    uint32_t frac_step_size,
    Gain::AScale amplitude_scale,
    bool accumulate) {
  if (amplitude_scale == Gain::UNITY) {
    return accumulate ? Mix<ScalerType::EQ_UNITY, true>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, amplitude_scale)
                      : Mix<ScalerType::EQ_UNITY, false>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, amplitude_scale);
  } else if (amplitude_scale < Gain::MuteThreshold(15)) {
    return Mix<ScalerType::MUTED, false>(dst, dst_frames, dst_offset, src,
                                         frac_src_frames, frac_src_offset,
                                         frac_step_size, amplitude_scale);
  } else if (amplitude_scale < Gain::UNITY) {
    return accumulate ? Mix<ScalerType::LT_UNITY, true>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, amplitude_scale)
                      : Mix<ScalerType::LT_UNITY, false>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, amplitude_scale);
  } else {
    return accumulate ? Mix<ScalerType::GT_UNITY, true>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, amplitude_scale)
                      : Mix<ScalerType::GT_UNITY, false>(
                            dst, dst_frames, dst_offset, src, frac_src_frames,
                            frac_src_offset, frac_step_size, amplitude_scale);
  }
}

// Templates used to expand all of the different combinations of the possible
// Point Sampler Mixer configurations.
template <size_t DChCount, typename SType, size_t SChCount>
static inline MixerPtr SelectPSM(const AudioMediaTypeDetailsPtr& src_format,
                                 const AudioMediaTypeDetailsPtr& dst_format) {
  return MixerPtr(new PointSamplerImpl<DChCount, SType, SChCount>());
}

template <size_t DChCount, typename SType>
static inline MixerPtr SelectPSM(const AudioMediaTypeDetailsPtr& src_format,
                                 const AudioMediaTypeDetailsPtr& dst_format) {
  switch (src_format->channels) {
    case 1:
      return SelectPSM<DChCount, SType, 1>(src_format, dst_format);
    case 2:
      return SelectPSM<DChCount, SType, 2>(src_format, dst_format);
    default:
      return nullptr;
  }
}

template <size_t DChCount>
static inline MixerPtr SelectPSM(const AudioMediaTypeDetailsPtr& src_format,
                                 const AudioMediaTypeDetailsPtr& dst_format) {
  switch (src_format->sample_format) {
    case AudioSampleFormat::UNSIGNED_8:
      return SelectPSM<DChCount, uint8_t>(src_format, dst_format);
    case AudioSampleFormat::SIGNED_16:
      return SelectPSM<DChCount, int16_t>(src_format, dst_format);
    default:
      return nullptr;
  }
}

MixerPtr PointSampler::Select(const AudioMediaTypeDetailsPtr& src_format,
                              const AudioMediaTypeDetailsPtr& dst_format) {
  switch (dst_format->channels) {
    case 1:
      return SelectPSM<1>(src_format, dst_format);
    case 2:
      return SelectPSM<2>(src_format, dst_format);
    default:
      return nullptr;
  }
}

}  // namespace mixers
}  // namespace audio
}  // namespace media
}  // namespace mojo
