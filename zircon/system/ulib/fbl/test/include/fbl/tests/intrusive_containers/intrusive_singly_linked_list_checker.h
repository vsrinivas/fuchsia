// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_TESTS_INTRUSIVE_CONTAINERS_INTRUSIVE_SINGLY_LINKED_LIST_CHECKER_H_
#define FBL_TESTS_INTRUSIVE_CONTAINERS_INTRUSIVE_SINGLY_LINKED_LIST_CHECKER_H_

#include <fbl/intrusive_hash_table.h>
#include <zxtest/zxtest.h>

namespace fbl {
namespace tests {
namespace intrusive_containers {

using ::fbl::internal::is_sentinel_ptr;

// There is not all that much we can sanity check about a singly linked list.
// Basically, all we know is that every link in the list (including head) needs
// to be non-null and that the last link in the chain is terminated with the
// proper sentinel value.
class SinglyLinkedListChecker {
 public:
  template <typename ContainerType>
  static void SanityCheck(const ContainerType& container) {
    using NodeTraits = typename ContainerType::NodeTraits;
    using PtrTraits = typename ContainerType::PtrTraits;

    typename PtrTraits::RawPtrType tmp = container.head_;
    while (true) {
      ASSERT_NOT_NULL(tmp);

      if (is_sentinel_ptr(tmp)) {
        ASSERT_EQ(container.sentinel(), tmp);
        break;
      }

      tmp = NodeTraits::node_state(*tmp).next_;
    }
  }
};

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace fbl

#endif  // FBL_TESTS_INTRUSIVE_CONTAINERS_INTRUSIVE_SINGLY_LINKED_LIST_CHECKER_H_
