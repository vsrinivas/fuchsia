// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_MESSAGE_TRANSCEIVER_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_MESSAGE_TRANSCEIVER_H_

#include <lib/async/cpp/wait.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>

#include <queue>

namespace media::audio::test {

class MessageTransceiver {
 public:
  struct Message {
    Message() = default;

    Message(uint32_t byte_count, uint32_t handle_count = 0)
        : bytes_(byte_count), handles_(handle_count) {}

    // Returns a T& overlaid on the message data.
    template <typename T>
    T& BytesAs() {
      FX_CHECK(sizeof(T) <= bytes_.size());
      return *reinterpret_cast<T*>(bytes_.data());
    }

    // Resizes the message data to sizeof(T) and Returns a T& overlaid on it.
    template <typename T>
    T& ResizeBytesAs() {
      bytes_.resize(sizeof(T));
      return BytesAs<T>();
    }

    std::vector<uint8_t> bytes_;
    std::vector<zx_handle_t> handles_;
  };

  using IncomingMessageCallback = fit::function<void(Message)>;
  using ErrorCallback = fit::function<void(zx_status_t)>;

  MessageTransceiver(async_dispatcher_t* dispatcher);

  ~MessageTransceiver();

  zx_status_t Init(zx::channel channel, IncomingMessageCallback incoming_message_callback,
                   ErrorCallback error_callback);

  const zx::channel& channel() const { return channel_; }

  void StopProcessing() { wait_.Cancel(); }
  void ResumeProcessing() {
    if (!wait_.is_pending()) {
      wait_.Begin(dispatcher_);
    }
  }
  zx_status_t ReadMessage();

  void Close();

  zx_status_t SendMessage(Message message);

 private:
  void OnError(zx_status_t status);

  // Tries to read messages from channel_ and waits for more.
  void ReadChannelMessages(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                           zx_status_t status, const zx_packet_signal_t* signal);

  async_dispatcher_t* dispatcher_;
  zx::channel channel_;
  IncomingMessageCallback incoming_message_callback_;
  ErrorCallback error_callback_;
  async::WaitMethod<MessageTransceiver, &MessageTransceiver::ReadChannelMessages> wait_{this};
  std::queue<Message> outbound_messages_;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_MESSAGE_TRANSCEIVER_H_
