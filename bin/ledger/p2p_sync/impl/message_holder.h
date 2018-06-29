// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_MESSAGE_HOLDER_H_
#define PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_MESSAGE_HOLDER_H_

#include <functional>
#include <utility>
#include <vector>

#include <lib/fit/function.h>
#include <lib/fxl/strings/string_view.h>

namespace p2p_sync {
// MessageHolder is used to hold a parsed Flatbuffer message along with its
// data.
template <class M>
class MessageHolder {
 public:
  // Create a new MessageHolder from the provided data and a parser.
  MessageHolder(fxl::StringView data,
                fit::function<const M*(const uint8_t*)> get_message)
      : data_(data.begin(), data.end()), message_(get_message(data_.data())) {}

  // Create a new MessageHolder from the provided data and a parser.
  MessageHolder(std::vector<uint8_t> data,
                fit::function<const M*(const uint8_t*)> get_message)
      : data_(std::move(data)), message_(get_message(data_.data())) {}

  // Create a new MessageHolder from the current object and a function to
  // specialize the message. The current message holder is destroyed in the
  // process. It can be used as follows:
  // MessageHolder<Message> message = ...;
  // MessageHolder<Request> request = message.TakeAndMap<Request>(
  //         [](const Message* message) {
  //             return static_cast<const Request*>(message->message());
  //         });
  template <class T>
  MessageHolder<T> TakeAndMap(fit::function<const T*(const M*)> get_message) {
    return MessageHolder<T>(std::move(*this), std::move(get_message));
  }

  // Allow moves.
  MessageHolder(MessageHolder&& other) noexcept = default;
  MessageHolder& operator=(MessageHolder&& other) noexcept = default;

  // Delete copy constructor and assignment. We cannot easily copy a
  // MessageHolder because of the need to re-parse the data and reapply the
  // chain of specializations to recreate the object.
  MessageHolder(const MessageHolder& other) = delete;
  MessageHolder& operator=(const MessageHolder<M>& other) = delete;

  // Allow MessageHolder<T> to be used as a T* for reading data.
  const M* operator->() const { return message_; }
  const M& operator*() const { return *message_; }

 private:
  template <typename U>
  friend class MessageHolder;

  // Equivalent constructor to the TakeAndMap factory. Using the factory allows
  // to explicitly declare template parameters, permitting the use of a lambda
  // directly.
  template <class T>
  MessageHolder(MessageHolder<T>&& other,
                fit::function<const M*(const T*)> get_message)
      : data_(std::move(other.data_)), message_(get_message(other.message_)) {
    other.message_ = nullptr;
  }

  std::vector<uint8_t> data_;
  const M* message_;
};

}  // namespace p2p_sync

#endif  // PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_MESSAGE_HOLDER_H_
