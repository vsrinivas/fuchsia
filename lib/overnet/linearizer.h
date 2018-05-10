// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include "sink.h"
#include "slice.h"

namespace overnet {

class Linearizer final : public Sink<Chunk>, public Source<Slice> {
 public:
  explicit Linearizer(uint64_t max_buffer);

  // Override Sink<Chunk> and Source<Slice>.
  void Close(const Status& status) override;

  // Override Sink<Chunk>.
  void Push(Chunk chunk, StatusCallback done) override;

  // Override Souce<Slice>.
  void Pull(StatusOrCallback<Optional<Slice>> ready) override;

 private:
  void IntegratePush(Chunk chunk, StatusCallback done);
  void ValidateInternals() const;

  const uint64_t max_buffer_;
  uint64_t offset_ = 0;
  std::map<uint64_t, std::pair<Slice, StatusCallback>> pending_push_;
  StatusOrCallback<Optional<Slice>> ready_;
  bool closed_ = false;
  Status closed_error_{Status::Ok()};
};

// Expects one Close() from Sink, and one from Source, then deletes itself.
class ReffedLinearizer final : public Sink<Chunk>, public Source<Slice> {
 public:
  static ReffedLinearizer* Make(uint64_t max_buffer) {
    return new ReffedLinearizer(max_buffer);
  }

  // Override Sink<Chunk> and Source<Slice>.
  void Close(const Status& status) override {
    impl_.Close(status);
    if (--refs_ == 0) delete this;
  }

  // Override Sink<Chunk>.
  void Push(Chunk chunk, StatusCallback done) override {
    impl_.Push(std::move(chunk), std::move(done));
  }

  // Override Souce<Slice>.
  void Pull(StatusOrCallback<Optional<Slice>> ready) override {
    impl_.Pull(std::move(ready));
  }

 private:
  explicit ReffedLinearizer(uint64_t max_buffer) : impl_(max_buffer) {}

  int refs_ = 2;
  Linearizer impl_;
};

}  // namespace overnet
