// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "src/connectivity/overnet/lib/environment/trace.h"
#include "src/connectivity/overnet/lib/stats/stream.h"
#include "src/connectivity/overnet/lib/vocabulary/callback.h"
#include "src/connectivity/overnet/lib/vocabulary/optional.h"
#include "src/connectivity/overnet/lib/vocabulary/slice.h"
#include "src/connectivity/overnet/lib/vocabulary/status.h"

namespace overnet {

class Linearizer final {
 public:
  explicit Linearizer(uint64_t max_buffer, StreamStats* stats);
  ~Linearizer();

  // Input interface.

  // Add a new slice to the input queue.
  // Returns true if successful, false on failure.
  [[nodiscard]] bool Push(Chunk chunk);

  void UpdateMaxBuffer(uint64_t new_max_buffer) {
    max_buffer_ = std::max(max_buffer_, new_max_buffer);
  }

  // Output interface.
  void Pull(StatusOrCallback<Optional<Slice>> ready);
  void PullAll(StatusOrCallback<Optional<std::vector<Slice>>> ready);

  // Returns a finalized status (safe to ignore).
  Status Close(const Status& status, Callback<void> quiesced);
  Status Close(const Status& status);

  bool IsComplete() const { return offset_ == length_; }

 private:
  void IntegratePush(Chunk chunk);
  void AssertValid(const char* marker, const char* pretty_function,
                   const char* file, int line) const;

#ifndef NDEBUG
  class CheckValid {
   public:
    CheckValid(const Linearizer* linearizer, const char* pretty_function,
               const char* file, int line)
        : linearizer_(linearizer),
          pretty_function_(pretty_function),
          file_(file),
          line_(line),
          last_(current_) {
      current_ = this;
      linearizer_->AssertValid("BEGIN", pretty_function_, file_, line_);
    }

    ~CheckValid() {
      if (linearizer_ != nullptr) {
        linearizer_->AssertValid("END", pretty_function_, file_, line_);
      }
      current_ = last_;
    }

    static void CedeChecksTo(Linearizer* linearizer) {
      for (auto* p = current_; p != nullptr; p = p->last_) {
        if (p->linearizer_ == linearizer) {
          p->linearizer_ = nullptr;
        }
      }
    }

   private:
    const Linearizer* linearizer_;
    const char* const pretty_function_;
    const char* const file_;
    int line_;
    // Not threadsafe (but nothing is).
    // It's not clear that we need to support nesting, but it's conceivable
    // through some callback sequence that we do, so current_/last_ keep track
    // of the current nesting structure.
    static inline CheckValid* current_ = nullptr;
    CheckValid* const last_;
  };
#endif

  uint64_t max_buffer_;
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
    StatusOrCallback<Optional<std::vector<Slice>>> done;
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
  StreamStats* const stats_;

  void IdleToClosed(const Status& status);
  void IdleToReadSlice(StatusOrCallback<Optional<Slice>> done);
  void IdleToReadAll(StatusOrCallback<Optional<std::vector<Slice>>> done);
  ReadSlice ReadSliceToIdle();
  ReadAll ReadAllToIdle();
  void ContinueReadAll();
};

}  // namespace overnet
