// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_ERR_OR_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_ERR_OR_H_

#include <lib/syslog/cpp/macros.h>

#include <variant>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

template <typename T>
class ErrOr {
  using Variant = std::variant<Err, T>;

 public:
  // The Err must be set when constructing this object in an error state.
  ErrOr(Err e) : variant_(std::move(e)) { FX_DCHECK(err().has_error()); }

  // Constructs with a value.
  ErrOr(T v) : variant_(std::move(v)) {}

  bool ok() const { return !std::holds_alternative<Err>(variant_); }
  bool has_error() const { return std::holds_alternative<Err>(variant_); }

  // Requires that has_error be true or this function will crash. See also err_or_empty().
  const Err& err() const {
    FX_DCHECK(has_error());
    FX_DCHECK(std::get<Err>(variant_).has_error());  // Err should be set if present.
    return std::get<Err>(variant_);
  }

  // Requires that has_error be false or this function will crash. See also [take_]value_or_empty().
  const T& value() const {
    FX_DCHECK(!has_error());
    return std::get<T>(variant_);
  }
  T& value() {
    FX_DCHECK(!has_error());
    return std::get<T>(variant_);
  }
  T take_value() {  // Destructively moves the value out.
    FX_DCHECK(!has_error());
    return std::move(std::get<T>(variant_));
  }

  // Implicit converstion to bool indicating "OK".
  explicit operator bool() const { return ok(); }

  // These functions are designed for integrating with code that uses an (Err, T) pair. If this
  // class isn't in the corresponding state, default constructs the missing object.
  //
  // The value version can optionally destructively move the value out (with the "take" variant)
  // since some values are inefficient to copy and often this is used in a context where the value
  // is no longer needed.
  //
  // The Err version does not allow destructive moving because it would leave this object in an
  // inconsistent state where the error object is stored in the variant, but err().has_error() is
  // not set. We assume that errors are unusual so are not worth optimizing for saving one string
  // copy to avoid this.
  Err err_or_empty() const {  // Makes a copy of the error.
    if (has_error())
      return std::get<Err>(variant_);
    return Err();
  }
  T value_or_empty() const {  // Makes a copy of the value.
    if (!has_error())
      return std::get<T>(variant_);
    return T();
  }
  T take_value_or_empty() {  // Destructively moves the value out.
    if (!has_error())
      return std::move(std::get<T>(variant_));
    return T();
  }

  // Adapts an old-style callback that takes two parameters and returns a newer one that takes an
  // ErrOr.
  static fit::callback<void(ErrOr<T>)> FromPairCallback(fit::callback<void(const Err&, T)> cb) {
    return [cb = std::move(cb)](ErrOr<T> value) mutable {
      cb(value.err_or_empty(), value.take_value_or_empty());
    };
  }

 private:
  Variant variant_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_ERR_OR_H_
