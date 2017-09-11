// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <fbl/intrusive_double_list.h>

#include "apps/bluetooth/lib/common/byte_buffer.h"
#include "apps/bluetooth/lib/common/intrusive_pointer_traits.h"
#include "apps/bluetooth/lib/common/packet_view.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace hci {

// A Packet is a move-only object that can be used to hold sent and received HCI packets. The Packet
// template is parameterized over the protocol packet header type.
//
// Instances of Packet cannot be created directly as the template does not specify the backing
// buffer, which should be provided by a subclass.
//
// Header-type-specific functionality can be provided in specializations of the Packet template.
//
// USAGE:
//
//   Each Packet consists of a PacketView into a buffer that actually stores the data. A buffer
//   should be provided in a subclass implementation. While the buffer must be sufficiently large
//   to store the packet, the packet contents can be much smaller.
//
//     template <typename HeaderType, size_t BufferSize>
//     class FixedBufferPacket : public Packet<HeaderType> {
//      public:
//       void Init(size_t payload_size) {
//         this->init_view(common::MutablePacketView<HeaderType>(&buffer_, payload_size));
//       }
//
//      private:
//       common::StaticByteBuffer<BufferSize> buffer_;
//     };
//
//     std::unique_ptr<Packet<MyHeaderType>> packet =
//         std::make_unique<FixedBufferPacket<MyHeaderType, 255>>(payload_size);
//
//   Use Packet::view() to obtain a read-only view into the packet contents:
//
//     auto foo = packet->view().header().some_header_field;
//
//   Use Packet::mutable_view() to obtain a mutable view into the packet, which allows the packet
//   contents and the size of the packet to be modified:
//
//     packet->mutable_view()->mutable_header()->some_header_field = foo;
//     packet->mutable_view()->set_payload_size(my_new_size);
//
//     // Copy data directly into the buffer.
//     auto mutable_bytes = packet->mutable_view()->mutable_bytes();
//     std::memcpy(mutable_bytes.mutable_data(), data, mutable_bytes.size());
//
// SPECIALIZATIONS:
//
//   Additional functionality that is specific to a protocol header type can be provided in a
//   specialization of the Packet template.
//
//     using MagicPacket = Packet<MagicHeader>;
//
//     template <>
//     class Packet<MagicHeader> : public PacketBase<MagicHeader, MagicPacket> {
//      public:
//       // Initializes packet with pancakes.
//       void InitPancakes();
//     };
//
//     // Create an instance of FixedBufferPacket declared above.
//     std::unique_ptr<MagicPacket> packet =
//         std::make_unique<FixedBufferPacket<MagicHeader, 255>>();
//     packet->InitPancakes();
//
//   This pattern is used by the CommandPacket, EventPacket, and ACLDataPacket classes (see
//   control_packets.h and acl_data_packet.h).
//
// THREAD-SAFETY:
//
//   Packet is NOT thread-safe without external locking.

// PacketBase provides the basic view and fbl::DoublyLinkedList functionality of a Packet. Intended
// to be inherited by the Packet template and all of its specializations.
template <typename HeaderType, typename T>
class PacketBase : public fbl::DoublyLinkedListable<std::unique_ptr<T>> {
 public:
  virtual ~PacketBase() = default;

  const common::PacketView<HeaderType>& view() const { return view_; }
  common::MutablePacketView<HeaderType>* mutable_view() { return &view_; }

 protected:
  PacketBase() = default;

  // Called by derived classes to initialize |view_| after initializing the corresponding buffer.
  void init_view(const common::MutablePacketView<HeaderType>& view) {
    FXL_DCHECK(!view_.is_valid());
    FXL_DCHECK(view.is_valid());
    view_ = view;
  }

 private:
  common::MutablePacketView<HeaderType> view_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PacketBase);
};

// The basic Packet template. See control_packets.h and acl_data_packet.h for specializations that
// add functionality beyond that of PacketBase.
template <typename HeaderType>
class Packet : public PacketBase<HeaderType, Packet<HeaderType>> {
 protected:
  Packet() = default;
};

}  // namespace hci
}  // namespace bluetooth
