// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/types/subpicture_stream_type.h"

#include "apps/media/src/util/safe_clone.h"

namespace mojo {
namespace media {

SubpictureStreamType::SubpictureStreamType(
    const std::string& encoding,
    std::unique_ptr<Bytes> encoding_parameters)
    : StreamType(StreamType::Medium::kSubpicture,
                 encoding,
                 std::move(encoding_parameters)) {}

SubpictureStreamType::~SubpictureStreamType() {}

const SubpictureStreamType* SubpictureStreamType::subpicture() const {
  return this;
}

std::unique_ptr<StreamType> SubpictureStreamType::Clone() const {
  return Create(encoding(), SafeClone(encoding_parameters()));
}

SubpictureStreamTypeSet::SubpictureStreamTypeSet(
    const std::vector<std::string>& encodings)
    : StreamTypeSet(StreamType::Medium::kSubpicture, encodings) {}

SubpictureStreamTypeSet::~SubpictureStreamTypeSet() {}

const SubpictureStreamTypeSet* SubpictureStreamTypeSet::subpicture() const {
  return this;
}

std::unique_ptr<StreamTypeSet> SubpictureStreamTypeSet::Clone() const {
  return Create(encodings());
}

}  // namespace media
}  // namespace mojo
