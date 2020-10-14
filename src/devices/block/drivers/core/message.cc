// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "message.h"

#include <zircon/device/block.h>

#include "server.h"

zx_status_t Message::Create(fbl::RefPtr<IoBuffer> iobuf, Server* server, block_fifo_request_t* req,
                            size_t block_op_size, MessageCompleter completer,
                            std::unique_ptr<Message>* out) {
  std::unique_ptr<Message> msg(new (block_op_size) Message(std::move(completer)));
  if (msg == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  msg->iobuf_ = std::move(iobuf);
  msg->server_ = server;
  msg->op_size_ = block_op_size;
  msg->result_ = ZX_OK;
  memcpy(&msg->req_, req, sizeof(msg->req_));
  memset(msg->_op_raw_, 0, block_op_size);
  *out = std::move(msg);
  return ZX_OK;
}

void Message::Complete() {
  completer_(result(), req_);
  server_->TxnEnd();
  iobuf_ = nullptr;
}
