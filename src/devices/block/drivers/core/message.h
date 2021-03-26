// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_CORE_MESSAGE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_CORE_MESSAGE_H_

#include <fuchsia/hardware/block/c/banjo.h>

#include <fbl/function.h>
#include <fbl/intrusive_double_list.h>

class IoBuffer;
class Server;

using MessageCompleter = fbl::Function<void(zx_status_t, block_fifo_request_t&)>;

// A single unit of work transmitted to the underlying block layer.
// Message contains a block_op_t, which is dynamically sized. Therefore, it implements its
// own allocator that takes block_op_size.
class Message final : public fbl::DoublyLinkedListable<Message*> {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Message);

  // Overloaded new operator allows variable-sized allocation to match block op size.
  void* operator new(size_t size) = delete;
  void* operator new(size_t size, size_t block_op_size) {
    return calloc(1, size + block_op_size - sizeof(block_op_t));
  }
  void operator delete(void* msg) { free(msg); }

  // Allocate a new, uninitialized Message whose block_op begins in a memory region that
  // is block_op_size bytes long.
  static zx_status_t Create(fbl::RefPtr<IoBuffer> iobuf, Server* server, block_fifo_request_t* req,
                            size_t block_op_size, MessageCompleter completer,
                            std::unique_ptr<Message>* out);

  // End the transaction specified by reqid and group, and release iobuf.
  void Complete();

  zx_status_t result() { return result_; }
  void set_result(zx_status_t res) { result_ = res; }

  block_op_t* Op() { return &op_; }

 private:
  explicit Message(MessageCompleter completer) : completer_(std::move(completer)) {}

  fbl::RefPtr<IoBuffer> iobuf_;
  MessageCompleter completer_;
  Server* server_;
  size_t op_size_;
  zx_status_t result_ = ZX_OK;
  block_fifo_request_t req_{};
  // Must be at the end of structure.
  union {
    block_op_t op_;
    uint8_t _op_raw_[1];  // Extra space for underlying block_op.
  };
};

#endif  // SRC_DEVICES_BLOCK_DRIVERS_CORE_MESSAGE_H_
