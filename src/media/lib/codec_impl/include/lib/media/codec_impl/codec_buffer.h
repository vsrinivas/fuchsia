// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_BUFFER_H_
#define SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_BUFFER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/media/codec_impl/codec_port.h>
#include <lib/media/codec_impl/codec_vmo_range.h>

#include <memory>

#include <fbl/macros.h>

class CodecImpl;
class CodecBufferForTest;

// Core codec representation of a video frame.  Different core codecs may have
// very different implementations of this.
//
// TODO(dustingreen): Have this be a base class that's defined by the
// CodecImpl source_set, and have amlogic-video VideoFrame derive from that base
// class.
//
// Regardless of codec, these will be managed by shared_ptr<>, because for
// decoder reference frames, shared_ptr<> makes sense.
struct VideoFrame;

// TODO(dustingreen): Support BufferCollection buffers.
//
// These are 1:1 with Codec buffers, but not necessarily 1:1 with core codec
// buffers.
//
// The const-ness of a CodecBuffer refers to the fields of the CodecBuffer
// instance, not to the data pointed at by buffer_base().
class CodecBuffer {
 public:
  // This is the same value as buffer_lifetime_ordinal in StreamProcessor FIDL.
  uint64_t lifetime_ordinal() const { return buffer_info_.lifetime_ordinal; }

  // This matches the buffer_index field of fuchsia::media::Packet when the packet refers to this
  // buffer.
  uint32_t index() const { return buffer_info_.index; }

  CodecPort port() const { return buffer_info_.port; }

  bool is_secure() const { return buffer_info_.is_secure; }
  // The vaddr of the start of the mapped VMO for this buffer.
  //
  // This will return nullptr if there's no VMO mapping because CPU access isn't
  // possible.  In that case the vaddr data pointer passed around regarding a
  // packet will be an offset into the buffer / VMO, and is only meaningful
  // with respect to a CodecBuffer that's also passed alongside.
  uint8_t* base() const;

  bool is_known_contiguous() const;

  // This will ZX_PANIC() if the buffer hasn't been pinned yet, or if !is_known_contiguous().
  zx_paddr_t physical_base() const;

  size_t size() const;

  // The main VMO.
  const zx::vmo& vmo() const;

  // The offset within the main VMO where data of this CodecBuffer starts.  The vmo_offset() is not
  // required to be divisible by ZX_PAGE_SIZE.
  uint64_t vmo_offset() const;

  // The use of weak_ptr<> here is to emphasize that we don't need shared_ptr<>
  // to keep the VideoFrame(s) alive.  We'd use a raw pointer here if it weren't
  // for needing to convert to a shared_ptr<> to call certain methods that
  // expect shared_ptr<>.
  //
  // This is marked const because it only mutates a mutable field, which is
  // considered mutable because it's about establishing an association between
  // video_frame and CodecBuffer after CodecBuffer has been constructed.
  void SetVideoFrame(std::weak_ptr<VideoFrame> video_frame) const;
  std::weak_ptr<VideoFrame> video_frame() const;

  // Unpin is automatic during ~CodecBuffer.
  zx_status_t Pin();
  bool is_pinned() const;

  void CacheFlush(uint32_t flush_offset, uint32_t length) const;
  void CacheFlushAndInvalidate(uint32_t flush_offset, uint32_t length) const;

 private:
  friend class CodecImpl;
  friend class std::unique_ptr<CodecBuffer>;
  friend struct std::default_delete<CodecBuffer>;
  friend class CodecBufferForTest;

  // Helper struct for encapsulating the properties of a Buffer
  struct Info {
    CodecPort port = kFirstPort;
    uint64_t lifetime_ordinal;
    uint32_t index;
    bool is_secure;
  };

  CodecBuffer(CodecImpl* parent, Info buffer_info, CodecVmoRange vmo_range);
  ~CodecBuffer();

  // Maps a page-aligned portion of the VMO including vmo_usable_start to vmo_usable_start +
  // vmo_usable_size.
  bool Map();

  // FakeMap() exists because most CodecAdapter(s) expect to have a CodecBuffer::base() and "data"
  // vaddr(s) within the buffer, even when buffers are secure.  IIUC, mapping to secure buffer +
  // cached policy on the VMO + speculative execution + aarch64 potentially would
  // randomly/spuriously fault even if the code never actually touched the mapping.  So instead of
  // mapping, we use a VMAR to reserve some vaddr space, but without any VMOs backing the VMAR, so
  // any actual accesses to any part of the VMAR will fault, and any speculative accesses won't
  // spuriuously/randomly fault.  We only need one VMAR across all buffers of a BufferCollection, so
  // CodecImpl passes in the vaddr of that VMAR here.  The fake_map_addr is in keeping with trying
  // to minimize the differences between non-secure and secure cases; it's just that we can't have
  // an actual mapping to the secure physical pages at the moment.  In addition, by not actually
  // mapping buffers we can't touch anyway, we presumably save some page table resources.
  //
  // The fake_map_addr is used as the a page-aligned base address for a fake mapping. Client code
  // must not touch memory at buffer_base() when a fake mapping is in effect, but if client code
  // does anyway, that thread will cleanly fault (not get stuck reading, not seem to let a write
  // happen, not be reading/writing any arbitrary other data in the process's address space).  The
  // fake_map_addr vaddr region is guaranteed to have enough vaddr pages to accomodate
  // vmo_usable_start % ZX_PAGE_SIZE + vmo_usable_size (so that an access within the bounds of the
  // buffer will reliably fault cleanly).
  void FakeMap(uint8_t* fake_map_addr);

  void CacheFlushInternal(uint32_t flush_offset, uint32_t length, bool also_invalidate) const;

  // The parent CodecImpl instance.  Just so we can call parent_->Fail().
  // The parent_ CodecImpl out-lives the CodecImpl::Buffer.
  CodecImpl* parent_;

  Info buffer_info_;

  // This msg still has the live vmo_handle.
  CodecVmoRange vmo_range_;

  // Mutable only in the sense that it's set later than the constructor.  The
  // association does not switch to a different VideoFrame once set.
  mutable std::weak_ptr<VideoFrame> video_frame_;

  // This accounts for vmo_usable_start.  The content bytes are not part of
  // a Buffer instance from a const-ness point of view.
  uint8_t* buffer_base_ = nullptr;
  // This remains false if fake_map_addr is passed to Map().  Not to be exposed to clients of
  // CodecBuffer.
  bool is_mapped_ = false;

  zx::pmt pinned_;
  // We use is_known_contiguous_ to check that physical_base() is only called after Pin() succeeded.
  // We check during Pin() that the VMO is really contiguous.
  bool is_known_contiguous_ = false;
  // This includes the low-order bits of the vmo_usable_start offset, so this is not necessarily
  // page-aligned.
  zx_paddr_t contiguous_paddr_base_ = {};

  DISALLOW_COPY_ASSIGN_AND_MOVE(CodecBuffer);
};

#endif  // SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_BUFFER_H_
