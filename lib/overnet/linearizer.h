// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include "optional.h"
#include "sink.h"
#include "slice.h"
#include "trace.h"

namespace overnet {

class Linearizer final : public Sink<Chunk>, public Source<Slice> {
 public:
  explicit Linearizer(uint64_t max_buffer, TraceSink trace_sink);
  ~Linearizer();

  // Override Sink<Chunk>.
  void Push(Chunk chunk) override;
  void Close(const Status& status, Callback<void> quiesced) override;

  // Override Source<Slice>.
  void Pull(StatusOrCallback<Optional<Slice>> ready) override;
  void PullAll(StatusOrCallback<std::vector<Slice>> ready) override;
  void Close(const Status& status) override;

 private:
  void IntegratePush(Chunk chunk);
  void ValidateInternals() const;

  const uint64_t max_buffer_;
  const TraceSink trace_sink_;
  uint64_t offset_ = 0;
  Optional<uint64_t> length_;
  std::map<uint64_t, Slice> pending_push_;

  enum class ReadMode {
    Closed,
    Idle,
    ReadSlice,
    ReadAll,
  };

  inline friend std::ostream& operator<<(std::ostream& out, ReadMode m) {
    switch (m) {
      case ReadMode::Closed:
        return out << "Closed";
      case ReadMode::Idle:
        return out << "Idle";
      case ReadMode::ReadSlice:
        return out << "ReadSlice";
      case ReadMode::ReadAll:
        return out << "ReadAll";
    }
  }

  struct Closed {
    Status status;
  };
  struct ReadSlice {
    StatusOrCallback<Optional<Slice>> done;
  };
  struct ReadAll {
    std::vector<Slice> building;
    StatusOrCallback<std::vector<Slice>> done;
  };
  union ReadData {
    ReadData() {}
    ~ReadData() {}

    Closed closed;
    ReadSlice read_slice;
    ReadAll read_all;
  };

  ReadMode read_mode_ = ReadMode::Idle;
  ReadData read_data_;

  void IdleToClosed(const Status& status);
  void IdleToReadSlice(StatusOrCallback<Optional<Slice>> done);
  void IdleToReadAll(StatusOrCallback<std::vector<Slice>> done);
  ReadSlice ReadSliceToIdle();
  ReadAll ReadAllToIdle();
  void ContinueReadAll();
};

// Expects one Close() from Sink, and one from Source, then deletes itself.
class ReffedLinearizer final : public Sink<Chunk>, public Source<Slice> {
 public:
  static ReffedLinearizer* Make(uint64_t max_buffer, TraceSink trace_sink) {
    return new ReffedLinearizer(max_buffer, trace_sink);
  }

  // Override Sink<Chunk> and Source<Slice>.

  // Override Sink<Chunk>.
  void Push(Chunk chunk) override { impl_.Push(std::move(chunk)); }

  void Close(const Status& status, Callback<void> quiesced) override {
    assert(quiesced_.empty());
    assert(!quiesced.empty());
    quiesced_ = std::move(quiesced);
    Close(status);
  }

  // Override Souce<Slice>.
  void Pull(StatusOrCallback<Optional<Slice>> ready) override {
    impl_.Pull(std::move(ready));
  }

  void Close(const Status& status) override {
    impl_.Close(status);
    if (--refs_ == 0) {
      quiesced_();
      delete this;
    }
  }

 private:
  explicit ReffedLinearizer(uint64_t max_buffer, TraceSink trace_sink)
      : impl_(max_buffer, trace_sink) {}

  int refs_ = 2;
  Linearizer impl_;
  Callback<void> quiesced_;
};

}  // namespace overnet
