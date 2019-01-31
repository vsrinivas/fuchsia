// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/alloc_checker.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>

namespace fuzzing {

// |fuzzing::StringList| is a small wrapper class used to make C-style strings easy to store and
// manipulate in a |fbl::DoublyLinkedList|.
class StringList final {
public:
    StringList();
    StringList(const char* const* elements, size_t num_elements);
    ~StringList();

    // Identical to |fbl::DoublyLinkedList<fbl::unique_ptr<StringElement>>::is_empty|.
    bool is_empty() const;

    // Identical to |fbl::DoublyLinkedList<fbl::unique_ptr<StringElement>>::size_slow|.
    size_t length() const;

    // These methods are similar to |fbl::DoublyLinkedList|'s, except that take raw C strings and
    // abstract away the process of wrapping them in the |StringElement| structure defined below.
    void push_front(const char* str);
    void push_back(const char* str);
    void push_front(const fbl::String& str) { return push_front(str.c_str()); }
    void push_back(const fbl::String& str) { return push_back(str.c_str()); }

    // These methods are similar to |fbl::DoublyLinkedList|'s, except that they apply a simple
    // substring pattern match instead of taking a functor.  Empty strings match everything, while
    // null inputs leave the list unchanged.

    // Keeps elements if they **CONTAIN** |substr|.  Null strings leave the list unchanged.
    void keep_if(const char* substr);
    void keep_if(const fbl::String& substr) { return keep_if(substr.c_str()); }

    // Keeps elements if they contain at least one element of |substrs|.
    void keep_if_any(StringList* substrs);

    // Keeps elements if they contain every element of |substrs|.
    void keep_if_all(StringList* substrs);

    // Removes elements if they exactly **MATCH** |match|.  Null strings leave the list unchanged.
    void erase_if(const char* match);
    void erase_if(const fbl::String& match) { return erase_if(match.c_str()); }

    // In place of iterators, this class provides |first| and |next| methods.  The former resets the
    // internal iterator to the beginning of the list, while the latter returns successive elements
    // with each successive call until it reaches the end of the list and returns null.  The list
    // can be simply iterated by:
    // for(const char *s = list.first(); s; s = list.next()) { ... }
    const char* first();
    const char* next();

    // Like|fbl::DoublyLinkedList<fbl::unique_ptr<StringElement>>::clear|, but also resets the
    // internal iterator.
    void clear();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(StringList);

    // |fuzzing::StringList::StringElement| is an internal intrusive container used to back the
    // strings in the list.
    struct StringElement final : public fbl::DoublyLinkedListable<fbl::unique_ptr<StringElement>> {
        fbl::String str_;
    };

    // Implements |push_front| and |push_back| above, depending on the value of |front|.
    void push(const char* str, bool front);

    // The actual list elements, and an iterator to return the |next| one.
    using List = fbl::DoublyLinkedList<fbl::unique_ptr<StringElement>>;
    List elements_;
    List::iterator iterator_;
};

} // namespace fuzzing
