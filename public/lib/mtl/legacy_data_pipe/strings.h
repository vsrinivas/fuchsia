// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_LEGACY_DATA_PIPE_STRINGS_H_
#define LIB_MTL_LEGACY_DATA_PIPE_STRINGS_H_

#include <string>

#include "lib/ftl/strings/string_view.h"
#include "mojo/public/cpp/system/data_pipe.h"

namespace mtl {

// Copies the data from |source| into |contents| and returns true on success and
// false on error. In case of I/O error, |contents| holds the data that could
// be read from source before the error occurred.
bool LegacyBlockingCopyToString(mojo::ScopedDataPipeConsumerHandle source,
                                std::string* contents);

bool LegacyBlockingCopyFromString(
    ftl::StringView source,
    const mojo::ScopedDataPipeProducerHandle& destination);

// Copies the string |contents| to a temporary data pipe and returns the
// consumer handle.
mojo::ScopedDataPipeConsumerHandle LegacyWriteStringToConsumerHandle(
    ftl::StringView source);

}  // namespace mtl

#endif  // LIB_MTL_LEGACY_DATA_PIPE_STRINGS_H_
