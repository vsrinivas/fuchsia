// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_TOOLS_FLOG_VIEWER_COUNTED_H_
#define APPS_MEDIA_TOOLS_FLOG_VIEWER_COUNTED_H_

#include <limits>

namespace mojo {
namespace flog {

// Counts things that are added or removed one at a time.
class Counted {
 public:
  // Returns the number of times |Add| was called.
  size_t count() const { return count_; }

  // Returns the number of times |Add| was called minus the number of times
  // |Remove| was called.
  size_t outstanding_count() const { return outstanding_count_; }

  // Returns the highest value attained by |outstanding_count|.
  size_t max_outstanding_count() const { return max_outstanding_count_; }

  // Adds one to the outstanding count.
  void Add() {
    ++count_;
    ++outstanding_count_;

    if (max_outstanding_count_ < outstanding_count_) {
      max_outstanding_count_ = outstanding_count_;
    }
  }

  // Subtracts one from the outstanding count.
  void Remove() { --outstanding_count_; }

 private:
  size_t count_ = 0;
  size_t outstanding_count_ = 0;
  size_t max_outstanding_count_ = 0;
};

}  // namespace flog
}  // namespace mojo

#endif  // APPS_MEDIA_TOOLS_FLOG_VIEWER_COUNTED_H_
