// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_LLVM_PROFDATA_INCLUDE_LIB_LLVM_PROFDATA_LLVM_PROFDATA_H_
#define SRC_LIB_LLVM_PROFDATA_INCLUDE_LIB_LLVM_PROFDATA_LLVM_PROFDATA_H_

#include <lib/stdcompat/span.h>

#include <cstdint>
#include <string_view>

class LlvmProfdata {
 public:
  // The object can be default-constructed and copied into, but cannot be used
  // in its default-constructed state.
  LlvmProfdata() = default;
  LlvmProfdata(const LlvmProfdata&) = default;
  LlvmProfdata& operator=(const LlvmProfdata&) = default;

  // This initializes the object based on the current module's own
  // instrumentation data.  This must be called before other methods below.
  void Init(cpp20::span<const std::byte> build_id);

  // This returns the size of the data blob to be published.
  // The return value is zero if there is no data to publish.
  size_t size_bytes() const { return size_bytes_; }

  // These return the offset and size within the blob of the aligned uint64_t[]
  // counters array.
  size_t counters_offset() const { return counters_offset_; }
  size_t counters_size_bytes() const { return counters_size_bytes_; }

  // If the data appears to be valid llvm-profdata format with a build ID, then
  // return the subspan that is just the build ID bytes themselves.  Otherwise
  // return an empty span.  This does only minimal format validation that is
  // sufficient to find the build ID safely, and does not guarantee that the
  // other sizes in the header are valid.
  static cpp20::span<const std::byte> BuildIdFromRawProfile(cpp20::span<const std::byte> data);

  // Return true if data appears to be a valid llvm-profdata dump whose build
  // ID matches the one passed to Init.
  bool Match(cpp20::span<const std::byte> data);

  // This must be passed a span of at least size_bytes() whose pointer must be
  // aligned to kAlign bytes.  Write the fixed metadata into the buffer, but
  // leave the counters area in the buffer untouched.  Returns the subspan
  // covering the counter data.
  cpp20::span<std::byte> WriteFixedData(cpp20::span<std::byte> data) {
    return DoFixedData(data, false);
  }

  // Verify the contents after Match(data) is true, causing assertion failures
  // if the data was corrupted.  After this, the data is verified to match what
  // WriteFixedData would have written.  Returns the subspan covering the
  // counter data, just as WriteFixedData would have done.
  cpp20::span<std::byte> VerifyMatch(cpp20::span<std::byte> data) {
    return DoFixedData(data, true);
  }

  // Copy out the current counter values from their link-time locations where
  // they have accumulated since startup.  The buffer must be at least
  // counters_size_bytes() and must be aligned as uint64_t, usually the return
  // value of WriteFixedData or VerifyMatch.
  void CopyCounters(cpp20::span<std::byte> data);

  // This is like CopyCounters, but instead of overwriting the buffer, it
  // merges the data with the existing counter values in the buffer.
  void MergeCounters(cpp20::span<std::byte> data);

  // This merges the from values into the to values.
  static void MergeCounters(cpp20::span<std::byte> to, cpp20::span<const std::byte> from);

  // After CopyCounters or MergeCounters has prepared the buffer, start using
  // it for live data updates.  This can be called again later to switch to a
  // different buffer.
  static void UseCounters(cpp20::span<std::byte> data);

  // This resets the runtime after UseCounters so that the original link-time
  // counter locations will be updated hereafter.  It's only used in tests.
  static void UseLinkTimeCounters();

  // The data blob must be aligned to 8 bytes in memory.
  static constexpr size_t kAlign = 8;

  // This is the name associated with the data in the fuchsia.debugdata FIDL
  // protocol.
  static constexpr std::string_view kDataSinkName = "llvm-profile";

  // This is a human-readable title used in log messages about the dump.
  static constexpr std::string_view kAnnounce = "LLVM Profile";

 private:
  cpp20::span<std::byte> DoFixedData(cpp20::span<std::byte> data, bool match);

  cpp20::span<const std::byte> build_id_;
  size_t size_bytes_ = 0;
  size_t counters_offset_ = 0;
  size_t counters_size_bytes_ = 0;
};

#endif  // SRC_LIB_LLVM_PROFDATA_INCLUDE_LIB_LLVM_PROFDATA_LLVM_PROFDATA_H_
