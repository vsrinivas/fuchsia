// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_LIST_OSTREAM_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_LIST_OSTREAM_H_

#include <iostream>

// Helper ostream wrapper to print a comma-commaarated list of items.
// Usage is the following:
//
//   1) Create instance from an existing std::ostream reference.
//   2) Print to the stream with << as usual.
//   3) Print the special value liststream::comma to indicate the end of
//      a list item (i.e. to insert a separator if needed). Trailing
//      commas will be ignored.
//
// E.g.:
//     list_ostream ls(os);
//     ls << "first:" << x << ls.comma << "second:" << y;
//
class list_ostream {
 public:
  list_ostream(std::ostream & os) : os_(os)
  {
  }

  // Change the separator string. E.g. set_comma(", ") to use a space after
  // each comma.
  void
  set_comma(const char * comma)
  {
    comma_ = comma;
  }
  // Print handler for generic values.
  template <typename T>
  list_ostream &
  operator<<(T value)
  {
    if (need_comma_)
      {
        os_ << comma_;
        need_comma_ = false;
      }

    os_ << value;
    return *this;
  }

  // Special print handling for list separators.
  struct separator_type
  {
  };

  list_ostream & operator<<(separator_type)
  {
    need_comma_ = true;
    return *this;
  }

  static constexpr separator_type comma = {};

 private:
  std::ostream & os_;
  bool           need_comma_ = false;
  const char *   comma_      = ",";
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_LIST_OSTREAM_H_
