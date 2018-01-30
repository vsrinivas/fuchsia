// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace wlan {

template <typename T> T* FromBytes(uint8_t* buf, size_t len) {
    if (len < sizeof(T)) return nullptr;
    return reinterpret_cast<T*>(buf);
}

template <typename T> const T* FromBytes(const uint8_t* buf, size_t len) {
    if (len < sizeof(T)) return nullptr;
    return reinterpret_cast<const T*>(buf);
}

// Port keys
//
// When waiting on a port, the key will have both a type and an id. The type is used for routing the
// packet to the correct handler. The id may be used by the handler to further route the packet
// within a subsystem (e.g., Mlme).

enum class PortKeyType : uint8_t {
    kDevice,
    kService,
    kMlme,
};

static inline uint64_t ToPortKey(PortKeyType type, uint64_t id) {
    return (id << 8) | static_cast<uint64_t>(type);
}

static inline PortKeyType ToPortKeyType(uint64_t key) {
    return static_cast<PortKeyType>(key & 0xff);
}

static inline uint64_t ToPortKeyId(uint64_t key) {
    return key >> 8;
}

// enum class cast helper
template <typename T>
static constexpr inline typename std::underlying_type<T>::type to_enum_type(T t) {
    return static_cast<typename std::underlying_type<T>::type>(t);
}

}  // namespace wlan
