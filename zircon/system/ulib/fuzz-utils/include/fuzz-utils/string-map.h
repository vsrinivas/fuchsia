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

    // In place of iterators, this class provides |first| and |next| methods.  The former resets the
    // internal iterator to the beginning of the map, while the latter returns successive key-value
    // pairs via |out_key| and |out_value| with each successive call until it reaches the end of the
    // map and returns null.  The map can be simply iterated by:
    //   const char *key;
    //   const char *value;
    //   map.begin();
    //   while(map.next(&key, &value)) { ... }
    void begin();
    bool next(const char** out_key, const char** out_val);
    bool next(fbl::String* out_key, fbl::String* out_val);

    // Returns the value for the given |key|, or null if the key isn't found.
    const char* get(const char* key) const;
    const char* get(const fbl::String& key) const { return get(key.c_str()); }

    // Associates the given |value| with the given |key|.  |key| and |value| must not be null.
    void set(const char* key, const char* val);
    void set(const fbl::String& key, const fbl::String& val) { set(key.c_str(), val.c_str()); }

    // Removes any value associated with the given |key| and removes it from the map.
    void erase(const char* key);
    void erase(const fbl::String& key) { erase(key.c_str()); }

    // Erases all keys.
    void clear();

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
