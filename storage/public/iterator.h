// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_PUBLIC_ITERATOR_H_
#define APPS_LEDGER_STORAGE_PUBLIC_ITERATOR_H_

#include "apps/ledger/storage/public/types.h"
#include "lib/ftl/macros.h"

namespace storage {

// A non-copyable iterator over a collection of elements of type T.
template <class T>
class Iterator {
 public:
  Iterator() {}
  virtual ~Iterator() {}

  // Advances to the next element in the collection.
  virtual Iterator<T>& Next() = 0;
  // Returns true iff no elements are available in the collection. It is
  // invalid to dereference this iterator if Valid() returns true.
  virtual bool Valid() const = 0;
  // Returns the current status of the iterator. If Valid() is false, indicates
  // why the iterator is not valid.
  virtual Status GetStatus() const = 0;

  virtual T& operator*() const = 0;
  virtual T* operator->() const = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Iterator);
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_PUBLIC_ITERATOR_H_
