// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RANGE_RANGE_H_
#define RANGE_RANGE_H_

#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <algorithm>
#include <type_traits>

namespace range {

// Provides default mechanisms for accessing and updating the range within the
// RangeContainer. This abstraction allows customized versions of |Range| to
// access these semantics from arbitrary containers.
template <typename RangeContainer, typename KeyType>
struct DefaultRangeTraits {
  // Returns the start of the range (inclusive).
  static KeyType Start(const RangeContainer& obj) { return obj.Start(); }

  // Returns the end of the range (exclusive).
  static KeyType End(const RangeContainer& obj) { return obj.End(); }

  // Updates |obj| with a new |start| and |end|.
  //
  // ARGUMENTS:
  //
  // If |other| is nullptr, then |obj| is being updated independently (typically,
  // when the range is shrinking).
  // If |other| is not nullptr, then |obj| is being updated due to a merge with |other|.
  // |obj| must not be nullptr.
  //
  // BEHAVIOR:
  //
  // - If the merge with |other| is valid, |Start()| and |End()| should be updated to return the
  // new values, and ZX_OK should be returned.
  // - If the merge with |other| is invalid, an error status should be returned, and |Start()|
  // and |End()| must return unmodified values.
  // - If |other| is null, then ZX_OK must be returned.
  static zx_status_t Update(const RangeContainer* other, KeyType start, KeyType end,
                            RangeContainer* obj) {
    // The default implementation has no reason to reject any merges.
    obj->Update(start, end);
    return ZX_OK;
  }
};

// Provides a default container for storing range values.
// If a custom container is utilized, custom traits may also be supplied to |Range|'s
// template arguments to alter how these fields are accessed.
template <typename KeyType>
class DefaultRangeContainer {
 public:
  DefaultRangeContainer() = default;
  DefaultRangeContainer(KeyType start, KeyType end) : start_(start), end_(end) {}
  [[nodiscard]] KeyType Start() const { return start_; }
  [[nodiscard]] KeyType End() const { return end_; }
  void Update(KeyType start, KeyType end) {
    start_ = start;
    end_ = end;
  }

 private:
  KeyType start_ = 0;
  KeyType end_ = 0;
};

// Range is a half closed interval [start, end).
//
// The values of the range are stored in a |Container| type, which holds these values.
// |ContainerTraits| describes how these values may be extracted from the |Container| type.
template <typename _KeyType = uint64_t, typename _Container = DefaultRangeContainer<_KeyType>,
          typename _ContainerTraits = DefaultRangeTraits<_Container, _KeyType>>
class Range {
 public:
  using KeyType = _KeyType;
  using Container = _Container;
  using ContainerTraits = _ContainerTraits;

  static_assert(std::is_unsigned_v<KeyType>);

  // Creates Range from [start, end).
  template <
      typename T = KeyType,
      std::enable_if_t<std::is_convertible<Container, DefaultRangeContainer<T>>::value, int> = 0>
  Range(KeyType start, KeyType end) : container_(start, end) {}

  explicit Range(Container container) : container_(std::move(container)) {}

  Range() = delete;
  Range(const Range&) = default;
  Range& operator=(const Range&) = default;
  Range& operator=(Range&&) = default;
  bool operator==(Range const& y) const { return (Start() == y.Start()) && (End() == y.End()); }
  bool operator!=(Range const& y) const { return (Start() != y.Start()) || (End() != y.End()); }
  ~Range() = default;

  [[nodiscard]] KeyType Start() const { return ContainerTraits::Start(container_); }
  [[nodiscard]] KeyType End() const { return ContainerTraits::End(container_); }

  // The length of the range is end - start. When start => end, then length is
  // considered as zero.
  [[nodiscard]] KeyType Length() const {
    if (End() <= Start()) {
      return KeyType(0);
    }
    return End() - Start();
  }

  // Merges another range into this one by modifying the start and end
  // of the current range object.
  //
  // Returns an error if the two ranges cannot be merged.
  zx_status_t Merge(const Range& other) {
    if (!Mergable(*this, other)) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    KeyType current_start = Start();
    KeyType current_end = End();
    KeyType new_start = std::min(Start(), other.Start());
    KeyType new_end = std::max(End(), other.End());

    zx_status_t status =
        ContainerTraits::Update(&other.container(), new_start, new_end, &container_);
    if (status != ZX_OK) {
      // |Start()| and |End()| must not change on error.
      ZX_DEBUG_ASSERT(current_start == Start());
      ZX_DEBUG_ASSERT(current_end == End());
    } else {
      // |Start()| and |End()| should have updated on success.
      ZX_DEBUG_ASSERT(new_start == Start());
      ZX_DEBUG_ASSERT(new_end == End());
    }

    return status;
  }

  // Extracts the container from underneath the range object.
  //
  // This operation invalidates the Range object.
  Container release() { return std::move(container_); }
  [[nodiscard]] const Container& container() const { return container_; }

 private:
  Container container_;
};

// Returns true if two extents overlap.
template <typename R>
bool Overlap(const R& x, const R& y) {
  if (x.Length() == 0 || y.Length() == 0) {
    return false;
  }

  auto max_start = std::max(x.Start(), y.Start());
  auto min_end = std::min(x.End(), y.End());

  return max_start < min_end;
}

// Returns true if two extents are adjacent. Two ranges are considered adjacent
// if one range starts right after another ends i.e. [a, b) [b, c] are
// adjacent ranges where a < b < c.
template <typename R>
bool Adjacent(const R& x, const R& y) {
  if (x.Length() == 0 || y.Length() == 0) {
    return false;
  }

  if (Overlap(x, y)) {
    return false;
  }

  auto max_start = std::max(x.Start(), y.Start());
  auto min_end = std::min(x.End(), y.End());

  return max_start == min_end;
}

// Two ranges are mergable is they either overlap or are adjacent.
template <typename R>
bool Mergable(const R& x, const R& y) {
  return Adjacent(x, y) || Overlap(x, y);
}

// Returns true if |x| contains |y|.
// Ex.
//    ASSERT_TRUE(Contains(Range(1, 10). Range(4, 8)));
//    ASSERT_TRUE(Contains(Range(1, 10). Range(1, 10)));
//    ASSERT_FALSE(Contains(Range(4, 8). Range(1, 10)));
//    ASSERT_FALSE(Contains(Range(1, 10). Range(5, 11)));
//    ASSERT_FALSE(Contains(Range(4, 8). Range(1, 5)));
template <typename R>
bool Contains(const R& x, const R& y) {
  if (x.Length() == 0 || y.Length() == 0) {
    return false;
  }

  return (x.Start() <= y.Start() && x.End() >= y.End());
}

extern template class Range<uint64_t>;
extern template bool Overlap<Range<uint64_t>>(const Range<uint64_t>& x, const Range<uint64_t>& y);
extern template bool Adjacent<Range<uint64_t>>(const Range<uint64_t>& x, const Range<uint64_t>& y);
extern template bool Mergable<Range<uint64_t>>(const Range<uint64_t>& x, const Range<uint64_t>& y);

}  // namespace range

#endif  // RANGE_RANGE_H_
