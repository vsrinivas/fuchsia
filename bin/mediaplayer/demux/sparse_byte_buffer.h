// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_DEMUX_SPARSE_BYTE_BUFFER_H_
#define GARNET_BIN_MEDIAPLAYER_DEMUX_SPARSE_BYTE_BUFFER_H_

#include <map>
#include <vector>

namespace media_player {

class SparseByteBuffer {
 public:
  struct Hole {
    Hole();
    Hole(const Hole& other);
    ~Hole();

    size_t position() { return iter_->first; }
    size_t size() { return iter_->second; }

   private:
    explicit Hole(std::map<size_t, size_t>::iterator iter);

    std::map<size_t, size_t>::iterator iter_;

    friend bool operator==(const Hole& a, const Hole& b);
    friend bool operator!=(const Hole& a, const Hole& b);
    friend class SparseByteBuffer;
  };

  struct Region {
    Region();
    Region(const Region& other);
    ~Region();

    size_t position() { return iter_->first; }
    size_t size() { return iter_->second.size(); }
    uint8_t* data() { return iter_->second.data(); }

   private:
    explicit Region(std::map<size_t, std::vector<uint8_t>>::iterator iter);

    std::map<size_t, std::vector<uint8_t>>::iterator iter_;

    friend bool operator==(const Region& a, const Region& b);
    friend bool operator!=(const Region& a, const Region& b);
    friend class SparseByteBuffer;
  };

  SparseByteBuffer();

  ~SparseByteBuffer();

  Hole null_hole() { return Hole(holes_.end()); }

  Region null_region() { return Region(regions_.end()); }

  // Initialized the buffer.
  void Initialize(size_t size);

  // Finds a region containing the specified position. This method will check
  // hint and its successor, if they're valid, before doing a search.
  Region FindRegionContaining(size_t position, Region hint);

  // Finds or creates a hole at the specified position. This method will check
  // hint, if it's valid, before doing a search.
  Hole FindOrCreateHole(size_t position, Hole hint);

  // Finds a hole containing the specified position.
  Hole FindHoleContaining(size_t position);

  // Creates a region that starts at hole.position(). The new region must not
  // overlap other existing regions and cannot extend beyond the size of this
  // sparse buffer. Holes are updated to accommodate the region. Fill returns
  // the first hole that follows the new region in the wraparound sense. If
  // this sparse buffer is completely filled (there are no holes), this method
  // return null_hole().
  Hole Fill(Hole hole, std::vector<uint8_t>&& buffer);

 private:
  using HolesIter = std::map<size_t, size_t>::iterator;
  using RegionsIter = std::map<size_t, std::vector<uint8_t>>::iterator;

  size_t size_ = 0u;
  std::map<size_t, size_t> holes_;                  // Hole sizes by position.
  std::map<size_t, std::vector<uint8_t>> regions_;  // Buffers by position.
};

bool operator==(const SparseByteBuffer::Hole& a,
                const SparseByteBuffer::Hole& b);

bool operator!=(const SparseByteBuffer::Hole& a,
                const SparseByteBuffer::Hole& b);

bool operator==(const SparseByteBuffer::Region& a,
                const SparseByteBuffer::Region& b);

bool operator!=(const SparseByteBuffer::Region& a,
                const SparseByteBuffer::Region& b);

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_DEMUX_SPARSE_BYTE_BUFFER_H_
