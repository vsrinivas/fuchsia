// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_MLME_TESTS_TEST_UTILS_H_
#define GARNET_LIB_WLAN_MLME_TESTS_TEST_UTILS_H_

#include <gtest/gtest.h>
#include <algorithm>
#include <lib/fidl/cpp/array.h>
#include <wlan/mlme/packet.h>

namespace wlan {
namespace test_utils {

template <typename T> struct IteratorTypes {
    typedef typename T::iterator iterator;
    typedef typename T::const_iterator const_iterator;
};

template <typename T, size_t N> struct IteratorTypes<T[N]> {
    typedef T* iterator;
    typedef const T* const_iterator;
};

template <typename T, size_t N> struct IteratorTypes<fidl::Array<T, N>> {
    typedef T* iterator;
    typedef const T* const_iterator;
};

template <typename T> struct RangeWrapper {
    explicit RangeWrapper(const T& range) : range(range) {}

    typedef typename IteratorTypes<T>::iterator iterator;
    typedef typename IteratorTypes<T>::const_iterator const_iterator;

    const T& range;

    const_iterator begin() const { return std::begin(range); }

    const_iterator end() const { return std::end(range); }

    template <typename U> bool operator==(const RangeWrapper<U>& other) const {
        return std::equal(begin(), end(), other.begin(), other.end());
    }
};

static inline fbl::unique_ptr<Packet> MakeWlanPacket(std::initializer_list<uint8_t> bytes) {
    auto packet = GetWlanPacket(bytes.size());
    ZX_ASSERT(packet != nullptr);

    std::copy(bytes.begin(), bytes.end(), packet->data());
    packet->set_len(bytes.size());
    return packet;
}

}  // namespace test_utils
}  // namespace wlan

#define EXPECT_RANGES_EQ(a, b) \
    EXPECT_EQ(test_utils::RangeWrapper((a)), test_utils::RangeWrapper((b)))

#define ASSERT_RANGES_EQ(a, b) \
    ASSERT_EQ(test_utils::RangeWrapper((a)), test_utils::RangeWrapper((b)))

#endif  // GARNET_LIB_WLAN_MLME_TESTS_TEST_UTILS_H_
