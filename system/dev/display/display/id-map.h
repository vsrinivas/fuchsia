// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_hash_table.h>

namespace display {

// Helper for allowing structs which are identified by unique ids to be put in a hashmap.
template <typename T> class IdMappable : public fbl::SinglyLinkedListable<T> {
public:
    int32_t id;

    static size_t GetHash(int32_t id) { return id; }

    int32_t GetKey() const { return id; }

    using Map = fbl::HashTable<int32_t, T>;
};

} // namespace display
