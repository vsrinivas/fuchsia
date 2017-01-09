// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <endian.h>

namespace media {

// Defines a protocol for MediaPlayer remoting.
class MediaPlayerNet {
 protected:
  template <typename T>
  T* NewMessage(std::vector<uint8_t>* message) {
    message->resize(sizeof(T));
    return new (message->data()) T;
  }

  enum class MessageType : uint8_t {
    kTimeCheck,
    kPlay,
    kPause,
    kSeek,
    kStatus
  };

  struct __attribute__((packed)) TimeCheckMessage {
    MessageType type_ = MessageType::kTimeCheck;
    int64_t sender_time_;
    int64_t receiver_time_;

    void NetToHost() {
      sender_time_ = be64toh(sender_time_);
      receiver_time_ = be64toh(receiver_time_);
    }

    void HostToNet() {
      sender_time_ = htobe64(sender_time_);
      receiver_time_ = htobe64(receiver_time_);
    }
  };

  struct __attribute__((packed)) PlayMessage {
    MessageType type_ = MessageType::kPlay;

    void NetToHost() {}
    void HostToNet() {}
  };

  struct __attribute__((packed)) PauseMessage {
    MessageType type_ = MessageType::kPause;

    void NetToHost() {}
    void HostToNet() {}
  };

  struct __attribute__((packed)) SeekMessage {
    MessageType type_ = MessageType::kSeek;
    int64_t position_;

    void NetToHost() { position_ = be64toh(position_); }
    void HostToNet() { position_ = htobe64(position_); }
  };

  struct __attribute__((packed)) StatusMessage {
    MessageType type_ = MessageType::kStatus;
    int64_t reference_time_;
    int64_t subject_time_;
    uint32_t reference_delta_;
    uint32_t subject_delta_;
    bool end_of_stream_;
    // TODO(dalesat): Include metadata and problem.
    uint64_t duration_;

    void NetToHost() {
      reference_time_ = be64toh(reference_time_);
      subject_time_ = be64toh(subject_time_);
      reference_delta_ = be32toh(reference_delta_);
      subject_delta_ = be32toh(subject_delta_);
    }

    void HostToNet() {
      reference_time_ = htobe64(reference_time_);
      subject_time_ = htobe64(subject_time_);
      reference_delta_ = htobe32(reference_delta_);
      subject_delta_ = htobe32(subject_delta_);
    }
  };
};

}  // namespace media
