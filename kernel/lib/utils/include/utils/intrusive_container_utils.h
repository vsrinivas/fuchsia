// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <utils/type_support.h>

namespace utils {
namespace internal {

// DirectEraseUtils
//
// A utility class used by HashTable to implement an O(n) or O(k) direct erase
// operation depending on whether or not the HashTable's bucket type supports
// O(k) erase.
template <typename ContainerType, typename Enable = void>
struct DirectEraseUtils;

template <typename ContainerType>
struct DirectEraseUtils<
        ContainerType,
        typename enable_if<ContainerType::SupportsConstantOrderErase == false, void>::type> {
    using PtrTraits = typename ContainerType::PtrTraits;
    using PtrType   = typename PtrTraits::PtrType;
    using ValueType = typename PtrTraits::ValueType;

    static PtrType erase(ContainerType& container, ValueType& obj) {
        return container.erase_if(
            [&obj](const ValueType& other) -> bool {
                return &obj == &other;
            });
    }
};

template <typename ContainerType>
struct DirectEraseUtils<
        ContainerType,
        typename enable_if<ContainerType::SupportsConstantOrderErase == true, void>::type> {
    using PtrTraits = typename ContainerType::PtrTraits;
    using PtrType   = typename PtrTraits::PtrType;
    using ValueType = typename PtrTraits::ValueType;

    static PtrType erase(ContainerType& container, ValueType& obj) {
        return container.erase(obj);
    }
};

// KeyEraseUtils
//
// A utility class used by HashTable to implement an O(n) or O(k) erase-by-key
// operation depending on whether or not the HashTable's bucket type is
// associative or not.
template <typename ContainerType, typename KeyTraits, typename Enable = void>
struct KeyEraseUtils;

template <typename ContainerType, typename KeyTraits>
struct KeyEraseUtils<
        ContainerType,
        KeyTraits,
        typename enable_if<ContainerType::IsAssociative == false, void>::type> {
    using PtrTraits = typename ContainerType::PtrTraits;
    using PtrType   = typename PtrTraits::PtrType;
    using ValueType = typename PtrTraits::ValueType;
    using KeyType   = typename KeyTraits::KeyType;

    static PtrType erase(ContainerType& container, const KeyType& key) {
        return container.erase_if(
            [key](const ValueType& other) -> bool {
                return KeyTraits::EqualTo(key, KeyTraits::GetKey(other));
            });
    }
};

template <typename ContainerType, typename KeyTraits>
struct KeyEraseUtils<
        ContainerType,
        KeyTraits,
        typename enable_if<ContainerType::IsAssociative == true, void>::type> {
    using PtrTraits = typename ContainerType::PtrTraits;
    using PtrType   = typename PtrTraits::PtrType;
    using KeyType   = typename KeyTraits::KeyType;

    static PtrType erase(ContainerType& container, const KeyType& key) {
        return container.erase(key);
    }
};

}  // namespace internal
}  // namespace utils
