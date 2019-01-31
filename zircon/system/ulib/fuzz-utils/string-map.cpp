// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <fuzz-utils/string-map.h>

#include <utility>

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

void StringMap::begin() {
    iterator_ = elements_.begin();
}

bool StringMap::next(const char** out_key, const char** out_val) {
    if (iterator_ == elements_.end()) {
        return false;
    }
    if (out_key) {
        *out_key = iterator_->key.c_str();
    }
    if (out_val) {
        *out_val = iterator_->val.c_str();
    }
    iterator_++;
    return true;
}

bool StringMap::next(fbl::String* out_key, fbl::String* out_val) {
    const char *key, *val;
    bool result = next(&key, &val);
    if (out_key) {
        out_key->Set(key);
    }
    if (out_val) {
        out_val->Set(val);
    }
    return result;
}

const char* StringMap::get(const char* key) const {
    ZX_DEBUG_ASSERT(key);
    auto iterator = elements_.find(key);
    return iterator == elements_.end() ? nullptr : iterator->val.c_str();
}

void StringMap::set(const char* key, const char* val) {
    ZX_DEBUG_ASSERT(key);
    ZX_DEBUG_ASSERT(val);
    fbl::AllocChecker ac;
    fbl::unique_ptr<StringElement> element(new (&ac) StringElement());
    ZX_ASSERT(ac.check());
    element->key.Set(key, &ac);
    ZX_ASSERT(ac.check());
    element->val.Set(val, &ac);
    ZX_ASSERT(ac.check());
    elements_.insert_or_replace(std::move(element));
    iterator_ = elements_.end();
}

void StringMap::erase(const char* key) {
    ZX_DEBUG_ASSERT(key);
    elements_.erase(key);
    iterator_ = elements_.end();
}

void StringMap::clear() {
    elements_.clear();
    iterator_ = elements_.end();
}

} // namespace fuzzing
