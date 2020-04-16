// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_UTILS_PATH_H_
#define SRC_STORAGE_VOLUME_IMAGE_UTILS_PATH_H_

#include <string>

namespace storage::volume_image {

// Returns the path where the current binary is being executed.
std::string GetBasePath();

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_UTILS_PATH_H_
