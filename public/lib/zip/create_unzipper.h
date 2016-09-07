// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZIP_CREATE_UNZIPPER_H_
#define LIB_ZIP_CREATE_UNZIPPER_H_

#include <vector>

#include "lib/zip/unique_unzipper.h"

namespace zip {

// Returns a UniqueUnzipper that unzips the contents of the buffer. The returned
// object contains a raw pointer to the given buffer, which means it is invalid
// to use the returned UniqueUnzipper after the given buffer has been destroyed.
UniqueUnzipper CreateUnzipper(std::vector<char>* buffer);

}  // namespace zip

#endif  // LIB_ZIP_CREATE_UNZIPPER_H_
