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

// There is not all that much we can sanity check about a singly linked list.
// Basically, all we know is that every link in the list (including head) needs
// to be non-null and that the last link in the chain is terminated with the
// proper sentinel value.
class SinglyLinkedListChecker {
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

        END_TEST;
    }
};

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace utils
