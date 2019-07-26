// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_PHYS_ITER_H_
#define DDK_PHYS_ITER_H_

#include <zircon/types.h>

__BEGIN_CDECLS

// An entry in a scatter gather list.
typedef struct {
  // length starting at the scatter gather entry offset, must be non zero
  size_t length;
  // offset relative to the buffer's vmo_offset
  size_t offset;
} phys_iter_sg_entry_t;

// Specifies the buffer to iterate over.
typedef struct {
  const zx_paddr_t* phys;  // list of physical addresses backing the buffer starting at vmo_offset
  uint64_t phys_count;     // number of entries in phys list

  size_t length;        // length of the buffer starting at vmo_offset, used if scatter gather
                        // list is not present
  uint64_t vmo_offset;  // offset into first page to start iterating on

  const phys_iter_sg_entry_t* sg_list;  // optional list of scatter gather entries to iterate over
  size_t sg_count;                      // number of entries in the scatter gather list
} phys_iter_buffer_t;

// Used to iterate over contiguous buffer ranges in the physical address space.
typedef struct {
  phys_iter_buffer_t buf;

  size_t total_iterated;  // total bytes iterated across all calls for this iterator
  zx_off_t offset;        // current offset in the segment (relative to the segment offset)
                          // i.e. the total number of bytes iterated for the current segment
  size_t max_length;      // max length to be returned by phys_iter_next()
  uint64_t page;          // index of page in buf->phys that contains offset
  uint64_t last_page;     // last valid page index in buf->phys

  size_t next_sg_entry_idx;  // next index in the scatter gather list
  size_t segment_offset;     // offset of the current scatter gather entry, relative to buffer
                             // vmo_offset, or zero if no scatter gather list is present.
  size_t segment_length;     // length of the buffer for the current scatter gather entry,
                             // or equal to buf.length if no scatter gather list is present.
} phys_iter_t;

// Initializes a phys_iter_t for iterating over physical memory.
// max_length is the maximum length of a range returned by phys_iter_next()
// max_length must be either a positive multiple of PAGE_SIZE, or zero for no limit.
void phys_iter_init(phys_iter_t* iter, const phys_iter_buffer_t* buf, size_t max_length);

// Returns the next physical address and length for the iterator up to size max_length.
// Return value is length, or zero if iteration is done.
size_t phys_iter_next(phys_iter_t* iter, zx_paddr_t* out_paddr);

__END_CDECLS

#ifdef __cplusplus

#include <utility>

namespace ddk {

// Wrapper around phys_iter_t that provides C++ iterator support.
class PhysIter {
 private:
  class iterator_impl;

 public:
  PhysIter(const phys_iter_buffer_t& buf, size_t max_length) {
    phys_iter_init(&iter_, &buf, max_length);
  }

  using PhysPair = std::pair<zx_paddr_t, size_t>;

  using const_iterator = iterator_impl;

  const_iterator begin() const { return const_iterator(iter_, false); }
  const_iterator cbegin() const { return const_iterator(iter_, false); }
  const_iterator end() const { return const_iterator(iter_, true); }
  const_iterator cend() const { return const_iterator(iter_, true); }

 private:
  class iterator_impl {
   public:
    iterator_impl(const iterator_impl& other) = default;
    iterator_impl& operator=(const iterator_impl& other) = default;

    bool operator==(const iterator_impl& other) const { return current_ == other.current_; }
    bool operator!=(const iterator_impl& other) const { return current_ != other.current_; }

    const PhysPair& operator*() const { return current_; }

    // Prefix
    iterator_impl& operator++() {
      if (current_.second == 0) {
        return *this;
      }
      current_.second = phys_iter_next(&iter_, &current_.first);
      return *this;
    }

    // Postfix
    iterator_impl operator++(int) {
      iterator_impl ret(*this);
      ++(*this);
      return ret;
    }

   private:
    friend class PhysIter;

    iterator_impl(const phys_iter_t& iter, bool last) : iter_(iter) {
      current_.second = phys_iter_next(&iter_, &current_.first);
      while (last && current_.second != 0) {
        current_.second = phys_iter_next(&iter_, &current_.first);
      };
    }

    phys_iter_t iter_;
    PhysPair current_ = {};
  };

  phys_iter_t iter_;
};

}  // namespace ddk
#endif

#endif  // DDK_PHYS_ITER_H_
