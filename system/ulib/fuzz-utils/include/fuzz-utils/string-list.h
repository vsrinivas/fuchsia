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
    ~StringList();

    // Identical to |fbl::DoublyLinkedList<fbl::unique_ptr<StringElement>>::is_empty|.
    bool is_empty() const;

    // Identical to |fbl::DoublyLinkedList<fbl::unique_ptr<StringElement>>::size_slow|.
    size_t length() const;

    // In place of iterators, this class provides |first| and |next| methods.  The former resets the
    // internal iterator to the beginning of the list, while the latter returns successive elements
    // with each successive call until it reaches the end of the list and returns null.  The list
    // can be simply iterated by:
    // for(const char *s = list.first(); s; s = list.next()) { ... }
    const char* first();
    const char* next();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(StringList);

    // |fuzzing::StringList::StringElement| is an internal intrusive container used to back the
    // strings in the list.
    struct StringElement final : public fbl::DoublyLinkedListable<fbl::unique_ptr<StringElement>> {
        fbl::String str_;
    };

    // The actual list elements, and an iterator to return the |next| one.
    using List = fbl::DoublyLinkedList<fbl::unique_ptr<StringElement>>;
    List elements_;
    List::iterator iterator_;
};

} // namespace fuzzing
