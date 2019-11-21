// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_SYNC_IMPL_MESSAGE_HOLDER_H_
#define SRC_LEDGER_BIN_P2P_SYNC_IMPL_MESSAGE_HOLDER_H_

#include <lib/fit/function.h>

#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "src/ledger/bin/p2p_sync/impl/encoding.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fxl/strings/string_view.h"

namespace p2p_sync {
// MessageHolder is used to hold a parsed Flatbuffer message along with its
// data.
template <class M>
class MessageHolder {
 public:
  // Create a new MessageHolder from the provided data and a parser.
  MessageHolder(std::unique_ptr<std::vector<uint8_t>> data, const M* message)
      : data_(std::move(data)), message_(message) {}

  // Create a new MessageHolder from the current object and a function to
  // specialize the message. The current message holder is destroyed in the
  // process. It can be used as follows:
  // MessageHolder<Message> message = ...;
  // MessageHolder<Request> request = std::move(message).TakeAndMap<Request>(
  //         [](const Message* message) {
  //             return static_cast<const Request*>(message->message());
  //         });
  template <class T>
  MessageHolder<T> TakeAndMap(fit::function<const T*(const M*)> get_message) && {
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
  MessageHolder(MessageHolder<T>&& other, fit::function<const M*(const T*)> get_message)
      : data_(std::move(other.data_)), message_(get_message(other.message_)) {
    other.message_ = nullptr;
  }

  std::unique_ptr<std::vector<uint8_t>> data_;
  const M* message_;
};

// Creates a new MessageHolder to contained a message, or nullopt if no message
// can be obtained.
template <class M>
std::optional<MessageHolder<M>> CreateMessageHolder(
    fxl::StringView data, fit::function<const M*(convert::ExtendedStringView)> get_message) {
  std::unique_ptr<std::vector<uint8_t>> data_vec =
      std::make_unique<std::vector<uint8_t>>(data.begin(), data.end());

  const M* message = get_message(*data_vec);

  if (!message) {
    return std::nullopt;
  }

  return MessageHolder<M>(std::move(data_vec), message);
}
}  // namespace p2p_sync

#endif  // SRC_LEDGER_BIN_P2P_SYNC_IMPL_MESSAGE_HOLDER_H_
