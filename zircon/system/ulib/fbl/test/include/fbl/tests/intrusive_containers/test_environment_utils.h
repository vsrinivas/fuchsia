// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_TESTS_INTRUSIVE_CONTAINERS_TEST_ENVIRONMENT_UTILS_H_
#define FBL_TESTS_INTRUSIVE_CONTAINERS_TEST_ENVIRONMENT_UTILS_H_

#include <lib/fit/defer.h>

#include <type_traits>
#include <utility>

namespace fbl {
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
                      typename std::enable_if<ContainerType::IsSequenced, void>::type> {
  using PtrTraits = typename ContainerType::PtrTraits;
  using PtrType = typename PtrTraits::PtrType;

  static void MoveInto(ContainerType& container, PtrType&& ptr) {
    container.push_front(std::move(ptr));
  }
};

template <typename ContainerType>
struct ContainerUtils<ContainerType,
                      typename std::enable_if<ContainerType::IsAssociative, void>::type> {
  using PtrTraits = typename ContainerType::PtrTraits;
  using PtrType = typename PtrTraits::PtrType;

  static void MoveInto(ContainerType& container, PtrType&& ptr) {
    container.insert(std::move(ptr));
  }
};

template <typename ContainerType, typename Enable = void>
struct SizeUtils;

template <typename ContainerType>
struct SizeUtils<ContainerType, typename std::enable_if<
                                    ContainerType::SupportsConstantOrderSize == true, void>::type> {
  static size_t size(const ContainerType& container) { return container.size(); }
};

template <typename ContainerType>
struct SizeUtils<
    ContainerType,
    typename std::enable_if<ContainerType::SupportsConstantOrderSize == false, void>::type> {
  static size_t size(const ContainerType& container) { return container.size_slow(); }
};

// If we make containers other than |container_| during a test, it is
// important to make sure that the container is properly cleared if it is a
// container of unmanaged pointers.  Containers of unmanaged pointers will
// DEBUG_ASSERT if they go out of scope with elements still in them, and the
// RAII nature of the testing framework means that if the test fails because
// of a test assert, it will simply return immediately.
//
// So, add a utility function which makes it simple to create an auto call
// which will handle the cleanup task for us.
template <typename ContainerType>
auto MakeContainerAutoCleanup([[maybe_unused]] ContainerType* container) {
  if constexpr (!ContainerType::PtrTraits::IsManaged) {
    return fit::defer([container]() { container->clear(); });
  } else {
    return fit::defer([]() {});
  }
}

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace fbl

#endif  // FBL_TESTS_INTRUSIVE_CONTAINERS_TEST_ENVIRONMENT_UTILS_H_
