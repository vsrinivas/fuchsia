// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxtl/string.h>

#include <string.h>

#include <magenta/assert.h>

#include <mxtl/algorithm.h>
#include <mxtl/atomic.h>
#include <mxtl/new.h>

namespace mxtl {

String::EmptyBuffer String::gEmpty;

void String::clear() {
    ReleaseRef(data_);
    InitWithEmpty();
}

int String::compare(const String& other) const {
    size_t len = min(length(), other.length());
    int retval = memcmp(data(), other.data(), len);
    if (retval == 0) {
        if (length() == other.length()) {
            return 0;
        }
        return length() < other.length() ? -1 : 1;
    }
    return retval;
}

void String::swap(String& other) {
    char* temp_data = data_;
    data_ = other.data_;
    other.data_ = temp_data;
}

String& String::operator=(const String& other) {
    AcquireRef(other.data_);
    ReleaseRef(data_); // release after acquire in case other == *this
    data_ = other.data_;
    return *this;
}

String& String::operator=(String&& other) {
    ReleaseRef(data_);
    data_ = other.data_;
    other.InitWithEmpty();
    return *this;
}

void String::Set(const char* data, size_t length) {
    char* temp_data = data_;
    Init(data, length);
    ReleaseRef(temp_data); // release after init in case data is within data_
}

void String::Set(const char* data, size_t length, mxtl::AllocChecker* ac) {
    char* temp_data = data_;
    Init(data, length, ac);
    ReleaseRef(temp_data); // release after init in case data is within data_
}

void String::Init(const char* data, size_t length) {
    if (length == 0u) {
        InitWithEmpty();
        return;
    }

    data_ = AllocData(length);
    memcpy(data_, data, length);
    data_[length] = 0u;
}

void String::Init(const char* data, size_t length, AllocChecker* ac) {
    if (length == 0u) {
        ac->arm(0u, true);
        InitWithEmpty();
        return;
    }

    data_ = AllocData(length, ac);
    if (!data_) {
        InitWithEmpty();
        return;
    }
    memcpy(data_, data, length);
    data_[length] = 0u;
}

void String::Init(size_t count, char ch) {
    if (count == 0u) {
        InitWithEmpty();
        return;
    }

    data_ = AllocData(count);
    memset(data_, ch, count);
    data_[count] = 0u;
}

void String::Init(size_t count, char ch, AllocChecker* ac) {
    if (count == 0u) {
        ac->arm(0u, true);
        InitWithEmpty();
        return;
    }

    data_ = AllocData(count, ac);
    if (!data_) {
        InitWithEmpty();
        return;
    }
    memset(data_, ch, count);
    data_[count] = 0u;
}

void String::InitWithEmpty() {
    gEmpty.ref_count.fetch_add(1u, memory_order_relaxed);
    data_ = &gEmpty.nul;
}

char* String::AllocData(size_t length) {
    void* buffer = operator new(buffer_size(length));
    return InitData(buffer, length);
}

char* String::AllocData(size_t length, AllocChecker* ac) {
    void* buffer = operator new(buffer_size(length), ac);
    if (!buffer)
        return nullptr;
    return InitData(buffer, length);
}

char* String::InitData(void* buffer, size_t length) {
    char* data = static_cast<char*>(buffer) + kDataFieldOffset;
    *length_field_of(data) = length;
    new (ref_count_field_of(data)) atomic_uint(1u);
    return data;
}

void String::AcquireRef(char* data) {
    ref_count_field_of(data)->fetch_add(1u, memory_order_relaxed);
}

void String::ReleaseRef(char* data) {
    unsigned int prior_count = ref_count_field_of(data)->fetch_sub(1u, memory_order_release);
    MX_DEBUG_ASSERT(prior_count != 0u);
    if (prior_count == 1u) {
        atomic_thread_fence(memory_order_acquire);
        operator delete(data - kDataFieldOffset);
    }
}

bool operator==(const String& lhs, const String& rhs) {
    return lhs.length() == rhs.length() &&
           memcmp(lhs.data(), rhs.data(), lhs.length()) == 0;
}

} // namespace mxtl
