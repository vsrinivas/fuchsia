// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "message.h"

#include <memory>

#include "server.h"

zx_status_t Message::Create(size_t block_op_size, std::unique_ptr<Message>* out) {
  Message* msg = new (block_op_size) Message();
  if (msg == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  msg->iobuf_ = nullptr;
  msg->server_ = nullptr;
  msg->op_size_ = block_op_size;
  *out = std::unique_ptr<Message>(msg);
  return ZX_OK;
}

void Message::Init(fbl::RefPtr<IoBuffer> iobuf, Server* server, block_fifo_request_t* req) {
  memset(_op_raw_, 0, op_size_);
  iobuf_ = std::move(iobuf);
  server_ = server;
  reqid_ = req->reqid;
  group_ = req->group;
}

void Message::Complete(zx_status_t status) {
  server_->TxnComplete(status, reqid_, group_);
  server_->TxnEnd();
  iobuf_ = nullptr;
}
