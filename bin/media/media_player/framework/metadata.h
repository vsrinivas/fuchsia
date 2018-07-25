// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_METADATA_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_METADATA_H_

#include <string>
#include <unordered_map>

namespace media_player {

using Metadata = std::unordered_map<std::string, std::string>;

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_METADATA_H_
