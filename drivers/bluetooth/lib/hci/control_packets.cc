// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "control_packets.h"

#include "lib/fxl/logging.h"

#include "slab_allocators.h"

namespace bluetooth {
namespace hci {
namespace slab_allocators {

// Slab-allocator traits for command packets.
using LargeCommandTraits =
    PacketTraits<CommandHeader, kLargeControlPacketSize, kNumLargeControlPackets>;
using SmallCommandTraits =
    PacketTraits<CommandHeader, kSmallControlPacketSize, kNumSmallControlPackets>;

// Slab-allocator traits for event packets. Since event packets are only received (and not sent) and
// because the packet size cannot be determined before the contents are read from the underlying
// channel, CommandChannel always allocates the largest possible buffer for events. Thus, a small
// buffer allocator is not needed.
using EventTraits = PacketTraits<EventHeader, kLargeControlPacketSize, kNumLargeControlPackets>;

using LargeCommandAllocator = fbl::SlabAllocator<LargeCommandTraits>;
using SmallCommandAllocator = fbl::SlabAllocator<SmallCommandTraits>;
using EventAllocator = fbl::SlabAllocator<EventTraits>;

}  // namespace slab_allocators

namespace {

std::unique_ptr<CommandPacket> NewCommandPacket(size_t payload_size) {
  FXL_DCHECK(payload_size <= slab_allocators::kLargeControlPayloadSize);

  if (payload_size <= slab_allocators::kSmallControlPayloadSize) {
    auto buffer = slab_allocators::SmallCommandAllocator::New(payload_size);
    if (buffer) return buffer;

    // We failed to allocate a small buffer; fall back to the large allocator.
  }

  return slab_allocators::LargeCommandAllocator::New(payload_size);
}

}  // namespace

// static
std::unique_ptr<CommandPacket> CommandPacket::New(OpCode opcode, size_t payload_size) {
  auto packet = NewCommandPacket(payload_size);
  if (!packet) return nullptr;

  packet->WriteHeader(opcode);
  return packet;
}

void CommandPacket::WriteHeader(OpCode opcode) {
  mutable_view()->mutable_header()->opcode = htole16(opcode);
  mutable_view()->mutable_header()->parameter_total_size = view().payload_size();
}

// static
std::unique_ptr<EventPacket> EventPacket::New(size_t payload_size) {
  return slab_allocators::EventAllocator::New(payload_size);
}

void EventPacket::InitializeFromBuffer() {
  mutable_view()->Resize(view().header().parameter_total_size);
}

}  // namespace hci
}  // namespace bluetooth

DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(::bluetooth::hci::slab_allocators::LargeCommandTraits,
                                      ::bluetooth::hci::slab_allocators::kMaxNumSlabs, true);
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(::bluetooth::hci::slab_allocators::SmallCommandTraits,
                                      ::bluetooth::hci::slab_allocators::kMaxNumSlabs, true);
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(::bluetooth::hci::slab_allocators::EventTraits,
                                      ::bluetooth::hci::slab_allocators::kMaxNumSlabs, true);
