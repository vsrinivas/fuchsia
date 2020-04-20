// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_MINFS_VMO_INDIRECT_H_
#define ZIRCON_SYSTEM_ULIB_MINFS_VMO_INDIRECT_H_

#include <lib/fzl/resizeable-vmo-mapper.h>
#include <minfs/bcache.h>

namespace minfs {

// Manages the indirect VMO for a vnode.
class VmoIndirect {
 public:
  // Provides access to the underlying VMO.
  class View {
   public:
    View(VmoIndirect* owner, uint32_t offset) : owner_(*owner), offset_(offset) {}

    View(View&) = default;
    View& operator =(View&) = delete;

    ~View() {
      // Assert that the data_ is still valid i.e. that the VMO did not grow or change in any other
      // way.
      ZX_ASSERT(data_ == nullptr || data_ == owner_.GetBlocks(offset_));
    }

    // Returns the value of the block at the given index.
    blk_t operator[](size_t index) const {
      return owner_.GetBlocks(offset_)[index];
    }

    // After calling this method, the VMO cannot be changed in any way that would invalidate the
    // mapped address i.e. don't call Grow.  The destructor for the view asserts this.  Prefer to
    // use the [] operator if possible, which doesn't have the same limitation.
    blk_t* data() const {
      if (data_ == nullptr) {
        data_ = owner_.GetBlocks(offset_);
      }
      return data_;
    }

   private:
    VmoIndirect& owner_;
    uint32_t offset_;
    mutable blk_t* data_ = nullptr;
  };

  VmoIndirect() = default;

  // Not copyable or movable.
  VmoIndirect(VmoIndirect&) = delete;
  VmoIndirect& operator =(VmoIndirect&) = delete;

  size_t size() const { return vmo_->size(); }
  const zx::vmo& vmo() const { return vmo_->vmo(); }
  storage::Vmoid& vmoid() { return vmoid_; }

  // Resizes the VMO.
  [[nodiscard]] zx_status_t Grow(size_t size) { return vmo_->Grow(size); }
  [[nodiscard]] zx_status_t Shrink(size_t size) { return vmo_->Shrink(size); }

  // Initializes the VMO, including attaching the VMO. The caller is responsible for
  // detaching before destruction.
  [[nodiscard]] zx_status_t Init(VnodeMinfs* vnode);

  // Returns true if the VMO has been initialized.
  bool IsValid() const { return vmo_ != nullptr; }

  // Resets to the uninitialized state.
  void Reset() { vmo_.reset(); }

  // Initializes the indirect VMO, and reads |count| indirect blocks from |iarray| into the indirect
  // VMO, starting at block offset |block|.
  [[nodiscard]] zx_status_t LoadIndirectBlocks(VnodeMinfs* vnode, const blk_t* iarray, uint32_t count,
                                               uint32_t block);

  // N.B. This function will assume that if the VMO is a given size, that *all* indirect blocks have
  // been loaded for the given size, but it will grow the VMO for this request. What this means is
  // that it's not safe to call this function with a non-sequential value of |dindex| i.e. don't
  // call this function with 3 and then call it with 2 and expect 2 to be loaded; you have to call
  // it with 2 and then 3. TODO(fxb/42096): This isn't ideal and we should refactor this at some
  // point.
  [[nodiscard]] zx_status_t LoadIndirectWithinDoublyIndirect(VnodeMinfs* vnode, uint32_t dindex);

  // Clears the block at |offset| in memory.
  // Assumes that vmo_indirect_ has already been initialized
  void ClearBlock(uint32_t block);

 private:
  friend class View;  // To access GetBlocks.

  // Returns a pointer to an array of blk_t for the given VMO block.
  blk_t* GetBlocks(uint32_t block) {
    return reinterpret_cast<blk_t*>(static_cast<uint8_t*>(vmo_->start()) + kMinfsBlockSize * block);
  }

  // vmo_ contains all indirect and doubly indirect blocks in the following order:
  // First kMinfsIndirect blocks                                - initial set of indirect blocks
  // Next kMinfsDoublyIndirect blocks                           - doubly indirect blocks
  // Next kMinfsDoublyIndirect * kMinfsDirectPerIndirect blocks - indirect blocks pointed to
  //                                                              by doubly indirect blocks
  std::unique_ptr<fzl::ResizeableVmoMapper> vmo_;
  storage::Vmoid vmoid_;
};

}  // namespace minfs

#endif  // ZIRCON_SYSTEM_ULIB_MINFS_VMO_INDIRECT_H_
