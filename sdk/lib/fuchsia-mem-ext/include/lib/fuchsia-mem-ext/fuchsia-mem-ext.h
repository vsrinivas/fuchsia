// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FUCHSIA_MEM_EXT_INCLUDE_LIB_FUCHSIA_MEM_EXT_FUCHSIA_MEM_EXT_H_
#define LIB_FUCHSIA_MEM_EXT_INCLUDE_LIB_FUCHSIA_MEM_EXT_FUCHSIA_MEM_EXT_H_

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/status.h>

#include <vector>

namespace fuchsia_mem_ext {

// Creates a new instance of Data with a copy of the data referred to by |data|.
// This will select the type of storage based on a default threshold:
//  - Data up to 16KB is stored inline
//  - Larger data is stored in a VMO.
//
// If data is stored in a VMO and |vmo_name| is set to a nonempty string, the VMO's
// name property is set to |vmo_name|.
//
// The cpp20::span<const uint8_t> overload always copies the data into the Data instance.
// The std::vector<uint8_t> overload will reuse the existing storage if the data will
// fit inline.
//
// Returns an error if a backing store cannot be created or the name cannot be set.
zx::result<fuchsia::mem::Data> CreateWithData(cpp20::span<const uint8_t> data,
                                              cpp17::string_view vmo_name = "");
zx::result<fuchsia::mem::Data> CreateWithData(std::vector<uint8_t> data,
                                              cpp17::string_view vmo_name = "");

// Creates a new instance of Data with a copy of the data referred to by |data|
// using a |size_threshold| to determine if the data should be stored inline.
// If the data's size is equal or less than the threshold, it will be stored
// inline. Otherwise, it will be stored in a VMO.
//
// Returns an error if a backing store cannot be created, the name cannot be
// set, or if |size_threshold| exceeds the limits of a single channel message.
zx::result<fuchsia::mem::Data> CreateWithData(cpp20::span<const uint8_t> data,
                                              size_t size_threshold,
                                              cpp17::string_view vmo_name = "");
zx::result<fuchsia::mem::Data> CreateWithData(std::vector<uint8_t> data, size_t size_threshold,
                                              cpp17::string_view vmo_name = "");

// Extracts the data from |data| and returns it to the caller. Consumes |data|.
//
// Returns an error if the data instance is malformed or if the data could not
// be read.
zx::result<std::vector<uint8_t>> ExtractData(fuchsia::mem::Data data);

}  // namespace fuchsia_mem_ext

#endif  // LIB_FUCHSIA_MEM_EXT_INCLUDE_LIB_FUCHSIA_MEM_EXT_FUCHSIA_MEM_EXT_H_
