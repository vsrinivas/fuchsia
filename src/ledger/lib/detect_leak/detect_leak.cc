// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements a memory leak detector. It is vastly inferior to what
// msan, or even asan can do, but do work in Fuchsia today. This must be
// replaced when msan or asan with leak detection when it is available on the
// platform.
//
// The principle is to run an application under ASAN and override new and
// delete. This code will then keep track of unrelease memory and when the
// number of unrelease memory reach a given threasold, find the stack trace that
// has is the origin for the most number of unallocated memory and displaying it
// to the user.
#if __has_feature(address_sanitizer)

#include <array>
#include <cstdint>
#include <map>
#include <mutex>

#include <sanitizer/asan_interface.h>

namespace {

// Maximum number of allocation to keep track.
constexpr size_t kKeepAlloc = 10000;

// Tracks elements to be able to find out which allocation are still valid.
// Constructing and using this container must not allocate memory, as it will be
// used in new and delete overrides.
//
// MaxSize: The maximum number of element in the container.
// A: The type of element in the container.
template <size_t MaxSize = kKeepAlloc, typename A = void *>
struct ElementTracker {
 public:
  // Inserts the given value in the container, returns false if the container is
  // full.
  bool Insert(A value) {
    std::lock_guard<std::mutex> lg(m_);
    if (size_ == MaxSize) {
      return false;
    }
    elements_[size_] = value;
    size_++;
    return true;
  }

  // Removes the given value from the container, returns false if the element is
  // not present.
  bool Remove(A value) {
    std::lock_guard<std::mutex> lg(m_);
    for (size_t i = 0; i < size_; ++i) {
      if (elements_[i] == value) {
        elements_[i] = elements_[size_ - 1];
        size_--;
        return true;
      }
    }
    return false;
  }

  // Returns the ith element of this container.
  A operator[](size_t i) const {
    std::lock_guard<std::mutex> lg(m_);
    return elements_[i];
  }

  // Returns the number of elements in the container.
  size_t size() const { return size_; };

 private:
  size_t size_ = 0;
  std::array<A, MaxSize> elements_;
  mutable std::mutex m_;
};

// Gets the global allocation tracker to use.
ElementTracker<> &GetAllocationTracker() {
  static ElementTracker<> *sAllocationSet = [] {
    void *memory reinterpret_cast<ElementTracker<> *>(malloc(sizeof(ElementTracker<>)));
    return new (memory) ElementTracker<>();
  }();
  return *sAllocationSet;
}

// Returns a signature of the stack trace that allocated the memory in |a|.
uintptr_t GetSignature(void *a) {
  void *stack[50];
  int thread_id;
  size_t nb_element = __asan_get_alloc_stack(a, stack, 50, &thread_id);
  uintptr_t result = thread_id;
  for (size_t i = 0; i < nb_element; ++i) {
    result = result ^ reinterpret_cast<uintptr_t>(stack[i]);
  }
  return result;
}

// Returns whether the memory in |a| is still allocated.
bool IsPointerAlive(void *a) {
  void *stack;
  int thread_id;
  return __asan_get_free_stack(a, &stack, 1, &thread_id) == 0;
}

// Removes all elements in the global allocation tracker that are not allocated
// anymore.
//
// Because of a bug, some allocation are done through new but freed by malloc.
// As this code only overrides new and delete, these allocations seem to be
// leaking while it is not the case. As asan keeps track of free and malloc, it
// is used to remove these spurious addresses.
void SanitizeSet() {
  size_t i = 0;
  while (i < GetAllocationTracker().size()) {
    void *p = GetAllocationTracker()[i];
    if (IsPointerAlive(p)) {
      ++i;
    } else {
      GetAllocationTracker().Remove(p);
    }
  }
}

// Wraps all allocations. This keeps track of all allocation. When the
// allocation tracker is full, this finds the stack that has the most allocated
// elements and uses ASAN to show it to the user.
void *WrapAlloc(void *p) {
  // |done| is true, once a leak has been found. This will short-circuit this
  // method so that allocation can be safely done when showing diagnostic
  // information.
  static bool done = false;
  if (done) {
    return p;
  }
  if (!GetAllocationTracker().Insert(p)) {
    // The tracker is full, but might contain adress that haven been created
    // with new and released with free (see |SanitizeSet|). Remove all of these
    // from the tracker, and check again.
    SanitizeSet();
    if (!GetAllocationTracker().Insert(p)) {
      // The tracker is now full of unreleased memory.
      done = true;

      // Count for each stack that did allocate memory, how many times it did
      // so.
      std::map<uintptr_t, size_t> counts;
      for (size_t i = 0; i < GetAllocationTracker().size(); ++i) {
        counts[GetSignature(GetAllocationTracker()[i])]++;
      }

      // Find the stack that allocated memory the most time.
      size_t max_count = 0;
      uintptr_t signature;
      for (const auto [s, c] : counts) {
        if (c > max_count) {
          max_count = c;
          signature = s;
        }
      }

      // Find an allocation allocated by the stack that allocated the most
      // memory.
      for (size_t i = 0; i < GetAllocationTracker().size(); ++i) {
        void *p = GetAllocationTracker()[i];
        if (GetSignature(p) == signature) {
          assert(IsPointerAlive(p));
          // Double free the pointer to trigger asan and get information about
          // the leak.
          free(p);
          free(p);
          assert(false);
        }
      }
    }
  }
  return p;
}

// Wraps all deallocations to remove these from the tracker.
void *WrapDealloc(void *p) {
  GetAllocationTracker().Remove(p);
  return p;
}

}  // namespace

#define INTERFACE __attribute__((visibility("default")))

INTERFACE void *operator new(size_t size) { return WrapAlloc(malloc(size)); }
INTERFACE void *operator new[](size_t size) { return WrapAlloc(malloc(size)); }
INTERFACE void *operator new(size_t size, std::nothrow_t const &) noexcept {
  return WrapAlloc(malloc(size));
}
INTERFACE void *operator new[](size_t size, std::nothrow_t const &) noexcept {
  return WrapAlloc(malloc(size));
}
INTERFACE void *operator new(size_t size, std::align_val_t align) {
  return WrapAlloc(malloc(size));
}
INTERFACE void *operator new[](size_t size, std::align_val_t align) {
  return WrapAlloc(malloc(size));
}
INTERFACE void *operator new(size_t size, std::align_val_t align, std::nothrow_t const &) noexcept {
  return WrapAlloc(malloc(size));
}
INTERFACE void *operator new[](size_t size, std::align_val_t align,
                               std::nothrow_t const &) noexcept {
  return WrapAlloc(malloc(size));
}

INTERFACE void operator delete(void *ptr) noexcept { free(WrapDealloc(ptr)); }
INTERFACE void operator delete[](void *ptr) noexcept { free(WrapDealloc(ptr)); }
INTERFACE void operator delete(void *ptr, std::nothrow_t const &) noexcept {
  free(WrapDealloc(ptr));
}
INTERFACE void operator delete[](void *ptr, std::nothrow_t const &) noexcept {
  free(WrapDealloc(ptr));
}
INTERFACE void operator delete(void *ptr, size_t size) noexcept { free(WrapDealloc(ptr)); }
INTERFACE void operator delete[](void *ptr, size_t size) noexcept { free(WrapDealloc(ptr)); }
INTERFACE void operator delete(void *ptr, std::align_val_t align) noexcept {
  free(WrapDealloc(ptr));
}
INTERFACE void operator delete[](void *ptr, std::align_val_t align) noexcept {
  free(WrapDealloc(ptr));
}
INTERFACE void operator delete(void *ptr, std::align_val_t align, std::nothrow_t const &) noexcept {
  free(WrapDealloc(ptr));
}
INTERFACE void operator delete[](void *ptr, std::align_val_t align,
                                 std::nothrow_t const &) noexcept {
  free(WrapDealloc(ptr));
}
INTERFACE void operator delete(void *ptr, size_t size, std::align_val_t align) noexcept {
  free(WrapDealloc(ptr));
}
INTERFACE void operator delete[](void *ptr, size_t size, std::align_val_t align) noexcept {
  free(WrapDealloc(ptr));
}

#endif
