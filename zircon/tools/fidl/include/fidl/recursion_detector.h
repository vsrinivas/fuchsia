// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <optional>
#include <vector>

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_RECURSION_DETECTOR_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_RECURSION_DETECTOR_H_

// |RecursionDetector| is a data structure that can be used to detect recursive/reentrant function
// calls. It keeps a stack of objects (pointers) that have been encountered before; if you attempt
// to re-add an already-encountered object to the stack, this is a signal that you have re-entered
// the same function.
//
// You can use this class to track recursion for more than just function calls. For example, the
// TypeShape visitor classes recursively call the same function, and that's OK; instead, they use
// this RecursionDetector class to detect whether the _parameter_ passed to the recursive function
// call is the same, in which case those functions have seen that object before, and need to break
// the recursion.
//
// To use this class:
//
// * Create a RecursionDetector object.
// * Call the Enter() method on entry to your function.
// * Pass in a pointer of any type to Enter(): the pointer serves as a "I've now encountered the
//   pointed-to object" marker to the current structure that you're traversing over.
// * Enter() will return a std::optional<Guard> object that you should assign to a local variable.
//   The Guard object will automatically pop the object off the recursion stack when your function
//   exits, via RAII.
// * If you pass in a pointer to Enter() that you've passed in before--i.e. that has been pushed
//   onto RecursionDetector's stack, and hasn't been popped yet--Enter() will return an empty
//   std::optional, which indicates that recursion has occurred.
// * Internally, the pointer you pass is cast to a void*.
class RecursionDetector {
 public:
  // See the comment on the RecursionDetector class for how to use this Guard class.
  class Guard {
   public:
    Guard() = delete;
    Guard(RecursionDetector* parent, const void* object) : parent_(parent) {
      parent_->seen_objects_.push_back(object);
    }

    Guard(const Guard&) = delete;
    Guard(const Guard&&) = delete;

    ~Guard() { parent_->seen_objects_.pop_back(); }

   private:
    RecursionDetector* parent_;
  };

  // See the comment on the RecursionDetector class for how to use this Enter() method.
  //
  // This method is templated for all pointer types, so that callers don't have to do an explicit
  // cast to const void* before passing in their pointer.
  template <typename T>
  [[nodiscard]] std::optional<Guard> Enter(const T* object) {
    const void* ptr = static_cast<const void*>(object);
    const bool seen_object =
        std::find(seen_objects_.cbegin(), seen_objects_.cend(), ptr) != seen_objects_.cend();
    if (seen_object) {
      return std::optional<Guard>();
    }

    return std::make_optional<Guard>(this, ptr);
  }

 private:
  std::vector<const void*> seen_objects_;
};

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_RECURSION_DETECTOR_H_
