// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/audio.h>
#include <magenta/types.h>
#include <mx/channel.h>
#include <mx/handle.h>
#include <mx/vmo.h>
#include <fbl/type_support.h>
#include <fbl/vector.h>

#include "drivers/audio/intel-hda/utils/codec-caps.h"

namespace audio {
namespace intel_hda {

mx_obj_type_t GetHandleType(const mx::handle& handle);

template <typename T>
mx_status_t ConvertHandle(mx::handle* abstract_handle, T* concrete_handle) {
    static_assert(fbl::is_base_of<mx::object<T>, T>::value,
                  "Target of ConvertHandle must be a concrete mx:: handle wrapper type!");

    if ((abstract_handle == nullptr) ||
        (concrete_handle == nullptr) ||
        !abstract_handle->is_valid())
        return MX_ERR_INVALID_ARGS;

    if (GetHandleType(*abstract_handle) != T::TYPE)
        return MX_ERR_WRONG_TYPE;

    concrete_handle->reset(abstract_handle->release());
    return MX_OK;
}

// Generate a vector of audio stream format ranges given the supplied sample
// capabilities and max channels.
mx_status_t MakeFormatRangeList(const SampleCaps& sample_caps,
                                uint32_t max_channels,
                                fbl::Vector<audio_stream_format_range_t>* ranges);


}  // namespace intel_hda
}  // namespace audio
