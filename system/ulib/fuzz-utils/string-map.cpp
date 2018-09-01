// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/intrusive_wavl_tree.h>
#include <fuzz-utils/string-map.h>

namespace fuzzing {

StringMap::StringMap() {
    iterator_ = elements_.end();
}

StringMap::~StringMap() {}

bool StringMap::is_empty() const {
    return elements_.is_empty();
}

size_t StringMap::size() const {
    return elements_.size();
}

} // namespace fuzzing
