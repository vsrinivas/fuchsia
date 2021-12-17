// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_POOL_MEM_CONFIG_H_
#define ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_POOL_MEM_CONFIG_H_

#include <zircon/boot/image.h>

#include <algorithm>

#include "pool.h"

namespace memalloc {

// memalloc::PoolMemConfig wraps reference to memalloc::Pool in a
// forward-iterable container-like type that yields zbi_mem_range_t.
// The resulting table coalesces adjacent ranges whose types reduce to
// the same basic type.

class PoolMemConfig {
 public:
  PoolMemConfig(const PoolMemConfig& other) = default;

  explicit PoolMemConfig(const Pool& pool) : pool_(pool) {}

  class iterator {
   public:
    using value_type = zbi_mem_range_t;
    using reference = value_type&;
    using pointer = value_type*;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    value_type operator*() const {
      return {
          .paddr = first_->addr,
          .length = size_,
          .type = ReduceType(*first_),
      };
    }

    bool operator==(const iterator& other) const { return first_ == other.first_; }
    bool operator!=(const iterator& other) const { return first_ != other.first_; }

    iterator& operator++() {  // prefix
      first_ = ++last_;
      size_ = 0;
      Coalesce();
      return *this;
    }

    iterator operator++(int) {  // postfix
      iterator old = *this;
      ++*this;
      return old;
    }

   private:
    friend PoolMemConfig;

    static constexpr uint32_t ReduceType(const Range& range) {
      return memalloc::IsExtendedType(range.type)  // Reduce to basic types.
                 ? ZBI_MEM_RANGE_RAM
                 : static_cast<uint32_t>(range.type);
    }

    void Coalesce() {
      while (last_ != end_) {
        size_ += last_->size;
        Pool::iterator next = std::next(last_);
        if (next == end_) {
          break;
        }
        if (next->type == Type::kTestRamReserve) {
          continue;
        }
        if (next->addr != last_->end() || ReduceType(*next) != ReduceType(*last_)) {
          break;
        }
        last_ = next;
      }
    }

    Pool::iterator first_, last_, end_;
    uint64_t size_ = 0;
  };

  // No size() method is provided because it's O(n).  Use std::distance.

  bool empty() const { return begin() == end(); }

  iterator begin() const {
    iterator it;
    it.first_ = it.last_ = pool_.begin();
    it.end_ = pool_.end();
    it.Coalesce();
    return it;
  }

  iterator end() const {
    iterator it;
    it.first_ = it.last_ = it.end_ = pool_.end();
    return it;
  }

 private:
  const Pool& pool_;
};

}  // namespace memalloc

#endif  // ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_POOL_MEM_CONFIG_H_
