// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_OVERNET_STREAM_H_
#define LIB_FIDL_CPP_OVERNET_STREAM_H_

#include <lib/fidl/cpp/message.h>
#include <lib/fit/function.h>
#include <zircon/types.h>

#include <map>

namespace overnet {

class FidlStream {
 public:
  virtual ~FidlStream();

  zx_status_t Process_(::fidl::Message message);

 protected:
  void Send_(fidl::Message message,
             fit::function<zx_status_t(fidl::Message)> callback) {
    Send_(AllocateCallback(std::move(callback)), std::move(message));
  }
  void Send_(zx_txid_t txid, fidl::Message message);
  virtual void Send_(fidl::Message message) = 0;

 private:
  // Implementations should ensure that no mutation is made to message when
  // returning ZX_ERR_NOT_SUPPORTED.
  virtual zx_status_t Dispatch_(::fidl::Message *message) = 0;

  zx_txid_t AllocateCallback(
      fit::function<zx_status_t(fidl::Message)> callback);

  std::map<zx_txid_t, fit::function<zx_status_t(fidl::Message)>> callbacks_;
  zx_txid_t next_txid_ = 1;
};

}  // namespace overnet

#endif
