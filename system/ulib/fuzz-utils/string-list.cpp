// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <fuzz-utils/string-list.h>

namespace fuzzing {

StringList::StringList() {
    iterator_ = elements_.end();
}

StringList::~StringList() {}

bool StringList::is_empty() const {
    return elements_.is_empty();
}

size_t StringList::length() const {
    return elements_.size_slow();
}

const char* StringList::first() {
    iterator_ = elements_.begin();
    return next();
}

const char* StringList::next() {
    return iterator_ != elements_.end() ? (iterator_++)->str_.c_str() : nullptr;
}

} // namespace fuzzing
