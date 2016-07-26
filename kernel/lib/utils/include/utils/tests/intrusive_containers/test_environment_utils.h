// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <utils/type_support.h>

namespace utils {
namespace tests {
namespace intrusive_containers {

// ContainerUtils
//
// A utility class used by container tests to move a pointer to an object into
// an instance of the container being tested.  For sequenced containers, the
// operation will be push_front().  For associative containers, the operation
// will be an insert() by key.
template <typename ContainerType, typename Enable = void>
struct ContainerUtils;

template <typename ContainerType>
struct ContainerUtils<ContainerType,
                      typename enable_if<ContainerType::IsSequenced, void>::type> {
    using PtrTraits = typename ContainerType::PtrTraits;
    using PtrType   = typename PtrTraits::PtrType;

    static void MoveInto(ContainerType& container, PtrType&& ptr) {
        container.push_front(utils::move(ptr));
    }
};

template <typename ContainerType>
struct ContainerUtils<ContainerType,
                      typename enable_if<ContainerType::IsAssociative, void>::type> {
    using PtrTraits = typename ContainerType::PtrTraits;
    using PtrType   = typename PtrTraits::PtrType;

    static void MoveInto(ContainerType& container, PtrType&& ptr) {
        container.insert(utils::move(ptr));
    }
};

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace utils
