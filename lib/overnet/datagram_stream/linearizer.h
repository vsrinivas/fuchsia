// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include "garnet/lib/overnet/environment/trace.h"
#include "garnet/lib/overnet/vocabulary/callback.h"
#include "garnet/lib/overnet/vocabulary/optional.h"
#include "garnet/lib/overnet/vocabulary/slice.h"
#include "garnet/lib/overnet/vocabulary/status.h"

namespace overnet {

class Linearizer final {
 public:
  explicit Linearizer(uint64_t max_buffer);
  ~Linearizer();

  // Input interface.

  // Add a new slice to the input queue.
  // Returns true if successful, false on failure.
  [[nodiscard]] bool Push(Chunk chunk);

  // Output interface.
  void Pull(StatusOrCallback<Optional<Slice>> ready);
  void PullAll(StatusOrCallback<std::vector<Slice>> ready);

  void Close(const Status& status, Callback<void> quiesced);
  void Close(const Status& status);

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
          line_(line) {
      linearizer_->AssertValid("BEGIN", pretty_function_, file_, line_);
    }

    ~CheckValid() {
      linearizer_->AssertValid("END", pretty_function_, file_, line_);
    }

   private:
    const Linearizer* const linearizer_;
    const char* const pretty_function_;
    const char* const file_;
    int line_;
  };
#endif

  const uint64_t max_buffer_;
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

}  // namespace overnet
