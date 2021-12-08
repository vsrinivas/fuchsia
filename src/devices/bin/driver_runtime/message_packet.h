// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_RUNTIME_MESSAGE_PACKET_H_
#define SRC_DEVICES_BIN_DRIVER_RUNTIME_MESSAGE_PACKET_H_

#include <lib/fdf/arena.h>
#include <lib/fdf/types.h>

#include <fbl/intrusive_double_list.h>

namespace driver_runtime {

class MessagePacket;

// Callable object for destroying uniquely owned message packets.
struct MessagePacketDestroyer {
  inline void operator()(MessagePacket* message_packet);
};

// MessagePacketOwner wraps a MessagePacket in a unique_ptr that has single
// ownership of the MessagePacket and deletes it whenever it falls out of scope.
using MessagePacketOwner = std::unique_ptr<MessagePacket, MessagePacketDestroyer>;

// Holds the contents of a message written to a channel.
// TODO(fxbug.dev/86856): we should consider recycling deleted packets.
class MessagePacket : public fbl::DoublyLinkedListable<MessagePacketOwner> {
 public:
  static MessagePacketOwner Create(fbl::RefPtr<fdf_arena_t> arena, void* data, uint32_t num_bytes,
                                   zx_handle_t* handles, uint32_t num_handles);

  // Copies the message contents to the parameters provided.
  // Returns ownership of an arena, the data and handles.
  void CopyOut(fdf_arena_t** out_arena, void** out_data, uint32_t* out_num_bytes,
               zx_handle_t** out_handles, uint32_t* out_num_handles);

  // fdf_channel_call treats the leading bytes of the payload as a transaction id of type
  // fdf_txid_t.
  zx_txid_t get_txid() const {
    if (num_bytes_ < sizeof(fdf_txid_t)) {
      return 0;
    }
    return *static_cast<fdf_txid_t*>(data_);
  }

  void set_txid(fdf_txid_t txid) {
    ZX_ASSERT(num_bytes_ >= sizeof(txid));
    *(static_cast<fdf_txid_t*>(data_)) = txid;
  }

  // Returns a reference to the arena.
  // The message packet retains a reference to correctly destruct itself.
  fbl::RefPtr<fdf_arena_t> arena() { return arena_; }
  uint32_t num_bytes() const { return num_bytes_; }
  uint32_t num_handles() const { return num_handles_; }

 private:
  // |MessagePacket| acquires a new reference to the arena written to the channel.
  // The arena is used to create the message packet, as well as being provided
  // to the user on |fdf_channel_write|. The user's reference to the arena will be
  // dropped when the user calls |fdf_arena_t_destroy|.
  MessagePacket(fbl::RefPtr<fdf_arena_t> arena, void* data, uint32_t num_bytes,
                zx_handle_t* handles, uint32_t num_handles)
      : arena_(std::move(arena)),
        data_(data),
        num_bytes_(num_bytes),
        handles_(handles),
        num_handles_(num_handles) {}

  // A private destructor helps to make sure that only our custom deleter is
  // ever used to destroy this object which, in turn, makes it very difficult
  // to not properly recycle the object.
  ~MessagePacket();

  void TakeData(void** out_data) {
    *out_data = data_;
    data_ = nullptr;
  }

  void TakeHandles(zx_handle_t** out_handles) {
    *out_handles = handles_;
    handles_ = nullptr;
  }

  friend struct MessagePacketDestroyer;
  static void Delete(MessagePacket* packet);

  fbl::RefPtr<fdf_arena_t> arena_;
  void* data_;
  uint32_t num_bytes_;
  zx_handle_t* handles_;
  uint32_t num_handles_;
};

// This can't be defined directly in the MessagePacketDestroyer struct definition
// because MessagePacket is an incomplete type at that point.
inline void MessagePacketDestroyer::operator()(MessagePacket* message_packet) {
  MessagePacket::Delete(message_packet);
}

}  // namespace driver_runtime

#endif  //  SRC_DEVICES_BIN_DRIVER_RUNTIME_MESSAGE_PACKET_H_
