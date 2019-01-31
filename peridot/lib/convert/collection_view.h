// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_CONVERT_COLLECTION_VIEW_H_
#define PERIDOT_LIB_CONVERT_COLLECTION_VIEW_H_

#include <algorithm>
#include <iterator>

#include <lib/fxl/logging.h>

namespace convert {

// View over the given collection. Doesn't take ownership of the collection that
// must outlives this class.
//
// Single-argument constructor is marked as NOLINT to suppress
// `google-explicit-constructor` clang-tidy warning - in this case the implicit
// conversion is intended.
template <typename T>
class CollectionView {
 public:
  using iterator = typename T::iterator;
  using const_iterator = typename T::const_iterator;

  CollectionView(T& collection)  // NOLINT
      : collection_(collection),
        begin_(std::begin(collection)),
        end_(std::end(collection)) {}
  CollectionView(T& collection, iterator begin, iterator end)
      : collection_(collection),
        begin_(std::move(begin)),
        end_(std::move(end)) {}
  CollectionView(const CollectionView& collection) = default;
  CollectionView& operator=(const CollectionView& collection) = default;

  const_iterator begin() const { return begin_; }
  const_iterator end() const { return end_; }
  iterator begin() { return begin_; }
  iterator end() { return end_; }

  // Returns a view over this view with the first element removed.
  CollectionView Tail() const {
    if (begin_ == end_) {
      return CollectionView<T>(collection_, end_, end_);
    }
    return CollectionView(collection_, std::next(begin_), end_);
  }

  // Returns a sub view of this view.
  CollectionView SubCollection(size_t begin, size_t length) const {
    if (begin >= size()) {
      return CollectionView<T>(collection_, end_, end_);
    }
    auto begin_it = std::next(begin_, begin);
    size_t final_length =
        std::min(length, static_cast<size_t>(std::distance(begin_it, end_)));
    return CollectionView(collection_, begin_it,
                          std::next(begin_it, final_length));
  }

  const auto& operator[](size_t pos) const {
    FXL_DCHECK(pos < size());
    return *std::next(begin_, pos);
  }

  auto& operator[](size_t pos) {
    FXL_DCHECK(pos < static_cast<size_t>(std::distance(begin_, end_)));
    return *std::next(begin_, pos);
  }

  bool empty() const { return begin_ == end_; }

  size_t size() const { return std::distance(begin_, end_); }

 private:
  T& collection_;
  iterator begin_;
  iterator end_;
};

}  // namespace convert

#endif  // PERIDOT_LIB_CONVERT_COLLECTION_VIEW_H_
