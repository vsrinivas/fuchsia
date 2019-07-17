// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_CPP_VMO_SNAPSHOT_H_
#define LIB_INSPECT_CPP_VMO_SNAPSHOT_H_

#include <unistd.h>

#include <functional>
#include <vector>

#include <lib/inspect/cpp/vmo/block.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

namespace inspect {

// |Snapshot| parses an incoming VMO buffer and produces a snapshot of
// the VMO contents. |Snapshot::Options| determines the behavior of
// snapshotting if a concurrent write potentially occurred.
//
// Example:
// Snapshot* snapshot;
// zx_status_t status = Snapshot::Create(std::move(vmo),
//   {.read_attempts = 1024, .skip_consistency_check = false},
//   &snapshot);
//
// Test Example:
// zx_status_t status = Snapshot::Create(std::move(vmo),
//   {.read_attempts = 1024, .skip_consistency_check = false},
//   std::make_unique<TestCallback>(),
//   &snapshot);
class Snapshot final {
 public:
  struct Options {
    // The number of attempts to read a consistent snapshot.
    // Reading fails if the number of attempts exceeds this number.
    int read_attempts;

    // If true, skip checking the buffer for consistency.
    bool skip_consistency_check;
  };

  // Type for observing reads on the VMO.
  using ReadObserver = std::function<void(uint8_t* buffer, size_t buffer_size)>;

  // By default, ensure consistency of the incoming Inspect VMO and retry up to
  // 1024 times.
  static constexpr Options kDefaultOptions = {.read_attempts = 1024,
                                              .skip_consistency_check = false};

  // Create a new snapshot of the given VMO and default options.
  static zx_status_t Create(const zx::vmo& vmo, Snapshot* out_snapshot);

  // Create a new snapshot of the given VMO and given options.
  static zx_status_t Create(const zx::vmo& vmo, Options options, Snapshot* out_snapshot);

  // Create a new snapshot of the given VMO, given options, and the given read observer
  // for observing snapshot operations.
  static zx_status_t Create(const zx::vmo& vmo, Options options, ReadObserver read_observer,
                            Snapshot* out_snapshot);

  // Create a new snapshot over the supplied immutable buffer. If the buffer
  // can not be interpreted as a snapshot, an error status is returned.
  // There are no observers or writers involved.
  static zx_status_t Create(std::vector<uint8_t> buffer, Snapshot* out_snapshot);

  Snapshot() = default;
  ~Snapshot() = default;
  Snapshot(Snapshot&&) = default;
  Snapshot& operator=(Snapshot&&) = default;

  explicit operator bool() const { return !buffer_.empty(); }

  // Returns the start of the snapshot data, if valid.
  const uint8_t* data() const { return buffer_.data(); }

  // Returns the size of the snapshot, if valid.
  size_t size() const { return buffer_.size(); }

  // Get a pointer to a block in the buffer by index.
  // Returns nullptr if the index is out of bounds.
  const Block* GetBlock(BlockIndex index) const;

 private:
  // Read from the VMO into a buffer.
  static zx_status_t Read(const zx::vmo& vmo, size_t size, uint8_t* buffer);

  // Parse the header from a buffer and obtain the generation count.
  static zx_status_t ParseHeader(uint8_t* buffer, uint64_t* out_generation_count);

  // Take a new snapshot of the VMO with default options.
  // If reading fails, the boolean value of the constructed |Snapshot| will be false.
  explicit Snapshot(std::vector<uint8_t> buffer);

  // The buffer storing the snapshot.
  std::vector<uint8_t> buffer_;
};

}  // namespace inspect

#endif  // LIB_INSPECT_CPP_VMO_SNAPSHOT_H_
