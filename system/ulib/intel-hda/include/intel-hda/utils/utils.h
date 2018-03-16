// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/audio.h>
#include <zircon/types.h>
#include <lib/zx/channel.h>
#include <lib/zx/handle.h>
#include <lib/zx/vmo.h>
#include <fbl/type_support.h>
#include <fbl/vector.h>

#include <intel-hda/utils/codec-caps.h>

namespace audio {
namespace intel_hda {

zx_obj_type_t GetHandleType(const zx::handle& handle);

template <typename T>
zx_status_t ConvertHandle(zx::handle* abstract_handle, T* concrete_handle) {
    static_assert(fbl::is_base_of<zx::object<T>, T>::value,
                  "Target of ConvertHandle must be a concrete zx:: handle wrapper type!");

    if ((abstract_handle == nullptr) ||
        (concrete_handle == nullptr) ||
        !abstract_handle->is_valid())
        return ZX_ERR_INVALID_ARGS;

    if (GetHandleType(*abstract_handle) != T::TYPE)
        return ZX_ERR_WRONG_TYPE;

    concrete_handle->reset(abstract_handle->release());
    return ZX_OK;
}

// Generate a vector of audio stream format ranges given the supplied sample
// capabilities and max channels.
zx_status_t MakeFormatRangeList(const SampleCaps& sample_caps,
                                uint32_t max_channels,
                                fbl::Vector<audio_stream_format_range_t>* ranges);


}  // namespace intel_hda
}  // namespace audio
