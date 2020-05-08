// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_FRAME_POINTER_H_
#define ZIRCON_KERNEL_PHYS_FRAME_POINTER_H_

#include <cstdint>

class FramePointer {
 public:
  FramePointer() = default;
  FramePointer(const FramePointer&) = default;
  FramePointer& operator=(const FramePointer&) = default;

  // A FramePointer is a forward iterator that also acts as its own container.
  // So in a range-based for loop it yields a list of uintptr_t PC values.
  using iterator = FramePointer;
  using const_iterator = iterator;
  using value_type = uintptr_t;

  // Container interface.

  iterator begin() const { return *this; }
  iterator end() const { return {}; }

  // Iterator interface.

  bool operator==(const FramePointer& fp) const { return fp.fp_ == fp_ && fp.pc_ == pc_; }
  bool operator!=(const FramePointer& fp) const { return !(*this == fp); }

  FramePointer& operator++();  // prefix

  FramePointer operator++(int) {  // postfix
    auto old = *this;
    ++*this;
    return old;
  }

  auto operator*() const { return pc_; }

  // The caller evaluates the default argument to supply its own backtrace:
  // `for (uintptr_t pc : FramePointer::BackTrace()) { ... }` or
  // `vector<uintptr_t>(FramePointer::BackTrace(), FramePointer::end())`.
  // That way the immediate caller itself is not included in the backtrace.
  static const FramePointer& BackTrace(
      const FramePointer* fp = reinterpret_cast<FramePointer*>(__builtin_frame_address(0))) {
    return *fp;
  }

 private:
  // This FP points to its caller's FP and PC.  A call pushes the PC and the
  // prologue then pushes the caller's FP (x86), or the prologue pushes the LR
  // and PC together (ARM); and then sets the FP to the SP.  Since the stack
  // grows down, the PC is always just after the FP in memory.
  const FramePointer* fp_ = nullptr;
  value_type pc_ = 0;
};

#endif  // ZIRCON_KERNEL_PHYS_FRAME_POINTER_H_
