// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INTEL_HDA_UTILS_UTILS_H_
#define INTEL_HDA_UTILS_UTILS_H_

#include <lib/zx/bti.h>
#include <lib/zx/channel.h>
#include <lib/zx/handle.h>
#include <lib/zx/vmo.h>
#include <zircon/device/audio.h>
#include <zircon/types.h>

#include <type_traits>
#include <utility>

#include <abs_clock/clock.h>
#include <fbl/function.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>
#include <intel-hda/utils/codec-caps.h>

namespace audio {
namespace intel_hda {

using abs_clock::Clock;
using abs_clock::RealClock;

static constexpr size_t MAX_HANDLER_CAPTURE_SIZE = sizeof(void*) * 2;
using WaitConditionFn = fbl::InlineFunction<bool(), MAX_HANDLER_CAPTURE_SIZE>;
zx_status_t WaitCondition(zx_duration_t timeout, zx_duration_t poll_interval, WaitConditionFn cond,
                          Clock* clock = RealClock::Get());

template <typename E>
constexpr typename std::underlying_type<E>::type to_underlying(E e) {
  return static_cast<typename std::underlying_type<E>::type>(e);
}

zx_obj_type_t GetHandleType(const zx::handle& handle);

// Utility class which manages a Bus Transaction Initiator using RefPtrs
// (allowing the BTI to be shared by multiple objects)
class RefCountedBti : public fbl::RefCounted<RefCountedBti> {
 public:
  static fbl::RefPtr<RefCountedBti> Create(zx::bti initiator);
  const zx::bti& initiator() const { return initiator_; }

 private:
  explicit RefCountedBti(zx::bti initiator) : initiator_(std::move(initiator)) {}
  zx::bti initiator_;
};

template <typename T>
zx_status_t ConvertHandle(zx::handle* abstract_handle, T* concrete_handle) {
  static_assert(std::is_base_of<zx::object<T>, T>::value,
                "Target of ConvertHandle must be a concrete zx:: handle wrapper type!");

  if ((abstract_handle == nullptr) || (concrete_handle == nullptr) || !abstract_handle->is_valid())
    return ZX_ERR_INVALID_ARGS;

  if (GetHandleType(*abstract_handle) != T::TYPE)
    return ZX_ERR_WRONG_TYPE;

  concrete_handle->reset(abstract_handle->release());
  return ZX_OK;
}

// Generate a vector of audio stream format ranges given the supplied sample
// capabilities and max channels.
zx_status_t MakeFormatRangeList(const SampleCaps& sample_caps, uint32_t max_channels,
                                fbl::Vector<audio_stream_format_range_t>* ranges);

}  // namespace intel_hda
}  // namespace audio

#endif  // INTEL_HDA_UTILS_UTILS_H_
