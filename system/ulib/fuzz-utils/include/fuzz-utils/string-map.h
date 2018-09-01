// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_wavl_tree.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>

namespace fuzzing {

// |fuzzing::StringMap| is a small wrapper class used to make C-style strings easy to store and
// manipulate in a |fbl::WAVLTree|.
class StringMap final {
public:
    StringMap();
    ~StringMap();

    // Identical to |fbl::WAVLTree<fbl::unique_ptr<StringElement>>::is_empty|.
    bool is_empty() const;

    // Identical to |fbl::WAVLTree<fbl::unique_ptr<StringElement>>::size|.
    size_t size() const;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(StringMap);

    // |fuzzing::StringMap::StringElement| is an internal intrusive container used to back the
    // strings in the list.
    struct StringElement final : public fbl::WAVLTreeContainable<fbl::unique_ptr<StringElement>> {
        fbl::String key;
        fbl::String val;
    };

    // |fuzzing::StringMap::KeyTraits| is used to sort keys lexicographically.
    struct KeyTraits {
        static const char* GetKey(const StringElement& element) { return element.key.c_str(); }
        static bool LessThan(const char* key1, const char* key2) { return strcmp(key1, key2) < 0; }
        static bool EqualTo(const char* key1, const char* key2) { return strcmp(key1, key2) == 0; }
    };

    // The actual map pairs, and an iterator to return the |next| one.
    using Map = fbl::WAVLTree<const char*, fbl::unique_ptr<StringElement>, KeyTraits>;
    Map elements_;
    Map::iterator iterator_;
};

} // namespace fuzzing
