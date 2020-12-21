// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/message_packet.h"

#include <stdint.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <cstddef>
#include <cstdint>
#include <new>

#include <fbl/algorithm.h>
#include <ktl/algorithm.h>

// MessagePackets have special allocation requirements because they can contain a variable number of
// handles and a variable size payload.
//
// To reduce heap fragmentation, MessagePackets are stored in a lists of fixed size buffers
// (BufferChains) rather than a contiguous blocks of memory.  These lists and buffers are allocated
// from the PMM.
//
// The first buffer in a MessagePacket's BufferChain contains the MessagePacket object, followed by
// its handles (if any), and finally its payload data (if any).

// The MessagePacket object, its handles and zx_txid_t must all fit in the first buffer.
static constexpr size_t kContiguousBytes =
    sizeof(MessagePacket) + (kMaxMessageHandles * sizeof(Handle*)) + sizeof(zx_txid_t);
static_assert(kContiguousBytes <= BufferChain::kContig, "");

// Handles are stored just after the MessagePacket.
static constexpr uint32_t kHandlesOffset = static_cast<uint32_t>(sizeof(MessagePacket));

// PayloadOffset returns the offset of the data payload from the start of the first buffer.
static inline uint32_t PayloadOffset(uint32_t num_handles) {
  // The payload comes after the handles.
  return kHandlesOffset + num_handles * static_cast<uint32_t>(sizeof(Handle*));
}

// Creates a MessagePacket in |msg| sufficient to hold |data_size| bytes and |num_handles|.
//
// Note: This method does not write the payload into the MessagePacket.
//
// Returns ZX_OK on success.
//
// static
inline zx_status_t MessagePacket::CreateCommon(uint32_t data_size, uint32_t num_handles,
                                               MessagePacketPtr* msg) {
  if (unlikely(data_size > kMaxMessageSize || num_handles > kMaxMessageHandles)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const uint32_t payload_offset = PayloadOffset(num_handles);

  // MessagePackets lives *inside* a list of buffers.  The first buffer holds the MessagePacket
  // object, followed by its handles (if any), and finally the payload data.
  BufferChain* chain = BufferChain::Alloc(payload_offset + data_size);
  if (unlikely(!chain)) {
    return ZX_ERR_NO_MEMORY;
  }
  DEBUG_ASSERT(!chain->buffers()->is_empty());
  chain->Skip(payload_offset);

  char* const data = chain->buffers()->front().data();
  Handle** const handles = reinterpret_cast<Handle**>(data + kHandlesOffset);

  // Construct the MessagePacket into the first buffer.
  MessagePacket* const packet = reinterpret_cast<MessagePacket*>(data);
  static_assert(kMaxMessageHandles <= UINT16_MAX, "");
  msg->reset(new (packet) MessagePacket(chain, data_size, payload_offset,
                                        static_cast<uint16_t>(num_handles), handles));
  // The MessagePacket now owns the BufferChain and msg owns the MessagePacket.

  return ZX_OK;
}

// static
zx_status_t MessagePacket::Create(user_in_ptr<const char> data, uint32_t data_size,
                                  uint32_t num_handles, MessagePacketPtr* msg) {
  MessagePacketPtr new_msg;
  zx_status_t status = CreateCommon(data_size, num_handles, &new_msg);
  if (unlikely(status != ZX_OK)) {
    return status;
  }
  status = new_msg->buffer_chain_->Append(data, data_size);
  if (unlikely(status != ZX_OK)) {
    return status;
  }
  *msg = ktl::move(new_msg);
  return ZX_OK;
}

// static
zx_status_t MessagePacket::Create(user_in_ptr<const zx_channel_iovec_t> iovecs, uint32_t num_iovecs,
                                  uint32_t num_handles, MessagePacketPtr* msg) {
  if (unlikely(num_iovecs > kMaxIovecsCount)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (num_iovecs <= kIovecChunkSize) {
    return CreateIovecBounded(iovecs, num_iovecs, num_handles, msg);
  }
  return CreateIovecUnbounded(iovecs, num_iovecs, num_handles, msg);
}

// static
zx_status_t MessagePacket::CreateIovecBounded(user_in_ptr<const zx_channel_iovec_t> user_iovecs,
                                              uint32_t num_iovecs, uint32_t num_handles,
                                              MessagePacketPtr* msg) {
  DEBUG_ASSERT(num_iovecs <= kIovecChunkSize);
  zx_channel_iovec_t iovecs[kIovecChunkSize];
  if (num_iovecs > 0) {
    zx_status_t status = user_iovecs.copy_array_from_user(iovecs, num_iovecs);
    if (unlikely(status != ZX_OK)) {
      return status;
    }
  }

  size_t message_size = 0;
  for (uint32_t i = 0; i < num_iovecs; i++) {
    if (unlikely(iovecs[i].reserved != 0)) {
      return ZX_ERR_INVALID_ARGS;
    }

    static_assert(sizeof(message_size) > sizeof(iovecs[i].capacity), "avoid overflow");
    message_size += iovecs[i].capacity;
  }

  MessagePacketPtr new_msg;
  zx_status_t status = CreateCommon(message_size, num_handles, &new_msg);
  if (unlikely(status != ZX_OK)) {
    return status;
  }

  for (uint32_t i = 0; i < num_iovecs; i++) {
    user_in_ptr<const char> src(reinterpret_cast<const char*>(iovecs[i].buffer));
    status = new_msg->buffer_chain_->Append(src, iovecs[i].capacity);
    if (unlikely(status != ZX_OK)) {
      return status;
    }
  }

  *msg = ktl::move(new_msg);
  return ZX_OK;
}

// static
zx_status_t MessagePacket::CreateIovecUnbounded(user_in_ptr<const zx_channel_iovec_t> user_iovecs,
                                                uint32_t num_iovecs, uint32_t num_handles,
                                                MessagePacketPtr* msg) {
  MessagePacketPtr new_msg;
  zx_status_t status = CreateCommon(kMaxMessageSize, num_handles, &new_msg);
  if (unlikely(status != ZX_OK)) {
    return status;
  }

  size_t message_size = 0;
  while (num_iovecs > 0) {
    uint32_t chunk_iovecs = ktl::min(num_iovecs, kIovecChunkSize);

    zx_channel_iovec_t iovecs[kIovecChunkSize];
    zx_status_t status = user_iovecs.copy_array_from_user(iovecs, chunk_iovecs);
    if (unlikely(status != ZX_OK)) {
      return status;
    }

    for (uint32_t i = 0; i < chunk_iovecs; i++) {
      const zx_channel_iovec_t* iovec = &iovecs[i];
      if (unlikely(iovec->reserved != 0)) {
        return ZX_ERR_INVALID_ARGS;
      }
      static_assert(sizeof(message_size) > sizeof(iovec->capacity), "avoid overflow");
      message_size += iovec->capacity;
      user_in_ptr<const char> src(reinterpret_cast<const char*>(iovec->buffer));
      status = new_msg->buffer_chain_->Append(src, iovec->capacity);
      if (unlikely(status != ZX_OK)) {
        return status;
      }
    }

    num_iovecs -= chunk_iovecs;
    user_iovecs = user_iovecs.element_offset(chunk_iovecs);
  }

  new_msg->buffer_chain_->FreeUnusedBuffers();
  new_msg->set_data_size(message_size);

  *msg = ktl::move(new_msg);
  return ZX_OK;
}

// static
zx_status_t MessagePacket::Create(const char* data, uint32_t data_size, uint32_t num_handles,
                                  MessagePacketPtr* msg) {
  MessagePacketPtr new_msg;
  zx_status_t status = CreateCommon(data_size, num_handles, &new_msg);
  if (unlikely(status != ZX_OK)) {
    return status;
  }
  status = new_msg->buffer_chain_->AppendKernel(data, data_size);
  if (unlikely(status != ZX_OK)) {
    return status;
  }
  *msg = ktl::move(new_msg);
  return ZX_OK;
}

void MessagePacket::recycle(MessagePacket* packet) {
  // Grab the buffer chain for this packet
  BufferChain* chain = packet->buffer_chain_;

  // Manually destruct the packet.  Do not delete it; its memory did not come
  // from new, it is contained as part of the buffer chain.
  packet->~MessagePacket();

  // Now return the buffer chain to where it came from.
  BufferChain::Free(chain);
}
