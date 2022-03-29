// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_INTERNAL_ASYNC_TXN_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_INTERNAL_ASYNC_TXN_H_

#include <zircon/assert.h>

#include <utility>

template <class... Args>
class AsyncTxn {
 public:
  using ReplyFunc = void (*)(void*, Args...);
  AsyncTxn(ReplyFunc reply, void* cookie) : reply_(reply), cookie_(cookie) {}
  ~AsyncTxn() {
    if (cookie_) {
      ZX_ASSERT_MSG(replied_, "Reply must be called on NetDevTxn");
    }
  }
  AsyncTxn(AsyncTxn&& other) noexcept { Move(std::move(other)); }
  AsyncTxn& operator=(AsyncTxn&& other) noexcept {
    Move(std::move(other));
    return *this;
  }

  void Reply(Args&&... args) {
    replied_ = true;
    reply_(cookie_, args...);
  }

 private:
  void Move(AsyncTxn&& other) {
    reply_ = other.reply_;
    cookie_ = nullptr;
    std::swap(cookie_, other.cookie_);
    replied_ = other.replied_;
  }
  ReplyFunc reply_ = nullptr;
  void* cookie_ = nullptr;
  bool replied_ = false;
};

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_INTERNAL_ASYNC_TXN_H_
