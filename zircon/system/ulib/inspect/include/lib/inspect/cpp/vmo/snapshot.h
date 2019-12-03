// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_CPP_VMO_SNAPSHOT_H_
#define LIB_INSPECT_CPP_VMO_SNAPSHOT_H_

#include <lib/fit/function.h>
#include <lib/inspect/cpp/vmo/block.h>
#include <lib/zx/vmo.h>
#include <unistd.h>
#include <zircon/types.h>

#include <functional>
#include <vector>

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
  struct Options final {
    // The number of attempts to read a consistent snapshot.
    // Reading fails if the number of attempts exceeds this number.
    int read_attempts = 1024;

    // If true, skip checking the buffer for consistency.
    bool skip_consistency_check = false;
  };

  // Type for observing reads on the VMO.
  using ReadObserver = fit::function<void(uint8_t* buffer, size_t buffer_size)>;

  // By default, ensure consistency of the incoming Inspect VMO and retry up to
  // 1024 times.
  static constexpr Options kDefaultOptions = {.read_attempts = 1024,
                                              .skip_consistency_check = false};

  // Create a new snapshot of the given VMO using default options.
  static zx_status_t Create(const zx::vmo& vmo, Snapshot* out_snapshot);

  // Create a new snapshot of the given VMO using the given options.
  static zx_status_t Create(const zx::vmo& vmo, Options options, Snapshot* out_snapshot);

  // Create a new snapshot of the given VMO using the given options, and use the read_observer
  // for observing snapshot operations.
  static zx_status_t Create(const zx::vmo& vmo, Options options, ReadObserver read_observer,
                            Snapshot* out_snapshot);

  // Create a new snapshot over the supplied buffer. If the buffer cannot be interpreted as a
  // snapshot, an error status is returned. There are no observers or writers involved.
  static zx_status_t Create(std::vector<uint8_t> buffer, Snapshot* out_snapshot);

  Snapshot() = default;
  ~Snapshot() = default;
  Snapshot(Snapshot&&) = default;
  Snapshot(const Snapshot&) = default;
  Snapshot& operator=(Snapshot&&) = default;
  Snapshot& operator=(const Snapshot&) = default;

  explicit operator bool() const { return buffer_ != nullptr && !buffer_->empty(); }

  // Returns the start of the snapshot data.
  const uint8_t* data() const { return buffer_ ? buffer_->data() : nullptr; }

  // Returns the size of the snapshot.
  size_t size() const { return buffer_ ? buffer_->size() : 0; }

 private:
  // Read from the VMO into a buffer.
  static zx_status_t Read(const zx::vmo& vmo, size_t size, uint8_t* buffer);

  // Parse the header from a buffer and obtain the generation count.
  static zx_status_t ParseHeader(uint8_t* buffer, uint64_t* out_generation_count);

  // Take a new snapshot of the VMO with default options.
  // If reading fails, the boolean value of the constructed |Snapshot| will be false.
  explicit Snapshot(std::vector<uint8_t> buffer);

  // The buffer storing the snapshot.
  std::shared_ptr<std::vector<uint8_t>> buffer_;
};

namespace internal {
// Get a pointer to a block in the snapshot by index.
// Returns nullptr if the index is out of bounds.
const Block* GetBlock(const Snapshot* snapshot, BlockIndex index);
}  // namespace internal

}  // namespace inspect

#endif  // LIB_INSPECT_CPP_VMO_SNAPSHOT_H_
