// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_PUBLIC_ITERATOR_H_
#define APPS_LEDGER_STORAGE_PUBLIC_ITERATOR_H_

#include "lib/ftl/macros.h"

namespace storage {

// A non-copyable iterator.
template <class T>
class Iterator {
 public:
  Iterator() {}
  virtual ~Iterator() {}

  virtual Iterator<T>& Next() = 0;
  virtual bool Done() = 0;

  virtual T& operator*() const = 0;
  virtual T* operator->() const = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Iterator);
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_PUBLIC_ITERATOR_H_
