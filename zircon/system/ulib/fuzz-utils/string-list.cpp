// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <fuzz-utils/string-list.h>

#include <utility>

namespace fuzzing {

StringList::StringList() {
    iterator_ = elements_.end();
}

StringList::StringList(const char* const* elements, size_t num_elements) {
    for (size_t i = 0; i < num_elements; ++i) {
        push_back(elements[i]);
    }
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

void StringList::push_front(const char* str) {
    push(str, true /* front */);
}

void StringList::push_back(const char* str) {
    push(str, false /* !front */);
}

void StringList::push(const char* str, bool front) {
    if (!str) {
        return;
    }
    fbl::AllocChecker ac;
    fbl::unique_ptr<StringElement> element(new (&ac) StringElement());
    ZX_ASSERT(ac.check());
    element->str_.Set(str, &ac);
    ZX_ASSERT(ac.check());
    if (front) {
        elements_.push_front(std::move(element));
    } else {
        elements_.push_back(std::move(element));
    }
    iterator_ = elements_.end();
}

void StringList::keep_if(const char* substr) {
    if (!substr) {
        return;
    }
    while (elements_.erase_if([substr](const StringElement& element) -> bool {
        return strstr(element.str_.c_str(), substr) == nullptr;
    })) {
    }
    iterator_ = elements_.end();
}

void StringList::keep_if_any(StringList* substrs) {
    ZX_DEBUG_ASSERT(substrs);
    while (elements_.erase_if([substrs](const StringElement& element) -> bool {
        for (const char* substr = substrs->first(); substr; substr = substrs->next()) {
            if (strstr(element.str_.c_str(), substr)) {
                return false;
            }
        }
        return true;
    })) {
    }
    iterator_ = elements_.end();
}

void StringList::keep_if_all(StringList* substrs) {
    ZX_DEBUG_ASSERT(substrs);
    while (elements_.erase_if([substrs](const StringElement& element) -> bool {
        for (const char* substr = substrs->first(); substr; substr = substrs->next()) {
            if (!strstr(element.str_.c_str(), substr)) {
                return true;
            }
        }
        return false;
    })) {
    }
    iterator_ = elements_.end();
}

void StringList::erase_if(const char* match) {
    if (!match) {
        return;
    }
    while (elements_.erase_if([match](const StringElement& element) -> bool {
        return strcmp(element.str_.c_str(), match) == 0;
    })) {
    }
    iterator_ = elements_.end();
}

void StringList::clear() {
    elements_.clear();
    iterator_ = elements_.begin();
}

} // namespace fuzzing
