// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <unittest.h>
#include <utils/intrusive_hash_table.h>

namespace utils {
namespace tests {
namespace intrusive_containers {

// Sanity checks for doubly linked lists are almost the same as those for singly
// linked lists.  We also check to be sure that the tail pointer is properly
// linked up (if the list is not empty) and that it is terminated with the
// sentinel value.
class DoublyLinkedListChecker {
public:
    template <typename ContainerType>
    static bool SanityCheck(const ContainerType& container) {
        using NodeTraits = typename ContainerType::NodeTraits;
        using PtrTraits  = typename ContainerType::PtrTraits;
        BEGIN_TEST;

        typename PtrTraits::RawPtrType tmp = PtrTraits::GetRaw(container.head_);
        while (true) {
            REQUIRE_NONNULL(tmp, "");

            if (PtrTraits::IsSentinel(tmp)) {
                REQUIRE_EQ(container.sentinel(), tmp, "");
                break;
            }

            tmp = PtrTraits::GetRaw(NodeTraits::node_state(*tmp).next_);
        }

        tmp = container.tail();
        if (!PtrTraits::IsSentinel(container.head_)) {
            REQUIRE_NONNULL(tmp, "");
            tmp = PtrTraits::GetRaw(NodeTraits::node_state(*tmp).next_);
        }
        REQUIRE_EQ(container.sentinel(), tmp, "");

        END_TEST;
    }
};

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace utils
