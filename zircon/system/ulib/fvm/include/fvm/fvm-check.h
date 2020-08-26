// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FVM_FVM_CHECK_H_
#define FVM_FVM_CHECK_H_

#include <stdarg.h>
#include <stdlib.h>

#include <utility>

#include <fbl/array.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fvm/format.h>

namespace fvm {

// Checker defines a class which may be used to validate an FVM
// (provided as either a regular file or a raw block device).
class Checker {
 public:
  Checker();
  Checker(fbl::unique_fd fd, uint32_t block_size, bool silent);
  ~Checker();

  // Sets the path of the block device / image to read the FVM from.
  void SetDevice(fbl::unique_fd fd) { fd_ = std::move(fd); }

  // Sets the block size of the provided device. Not automatically queried from the underlying
  // device, since this checker may operate on a regular file, which does not have an
  // attached block size.
  void SetBlockSize(uint32_t block_size) { block_size_ = block_size; }

  // Toggles the output of future calls to |Log|.
  void SetSilent(bool silent) { logger_.SetSilent(silent); }

  // Read from and validate the provided device, logging information if requested.
  bool Validate() const;

 private:
  class Logger {
   public:
    Logger() : silent_(false) {}
    explicit Logger(bool silent) : silent_(silent) {}

    // Toggles the output of future calls to |Log|.
    void SetSilent(bool silent) { silent_ = silent; }

    // Prints the format string and arguments to stderr.
    void Error(const char* format, ...) const {
      va_list arg;
      va_start(arg, format);
      vprintf(format, arg);
      va_end(arg);
    }

    // Prints the format string and arguments to stdout, unless explicitly silenced.
    void Log(const char* format, ...) const {
      va_list arg;
      if (!silent_) {
        va_start(arg, format);
        vprintf(format, arg);
        va_end(arg);
      }
    }

   private:
    bool silent_;
  };

  // Cached information from loading and validating the FVM.
  struct FvmInfo {
    // Contains both copies of metadata.
    fbl::Array<uint8_t> metadata;
    size_t valid_metadata_offset;
    const uint8_t* valid_metadata;
    const uint8_t* invalid_metadata;
    size_t block_size;
    size_t block_count;
    size_t device_size;
    size_t slice_size;
  };

  struct Slice {
    uint64_t virtual_partition;
    uint64_t virtual_slice;
    uint64_t physical_slice;
  };

  struct Partition {
    bool Allocated() const { return entry != nullptr; }

    const fvm::VPartitionEntry* entry = nullptr;
    fbl::Vector<Slice> slices;
  };

  // Parses the FVM info from the device, and validate it (minimally).
  bool LoadFVM(FvmInfo* out) const;

  // Outputs and checks information about the FVM, optionally logging parsed information.
  bool CheckFVM(const FvmInfo& info) const;

  // Acquires a list of slices and partitions while parsing the FVM.
  //
  // Returns false if the FVM contains contradictory or invalid data.
  bool LoadPartitions(const size_t slice_count, const fvm::SliceEntry* slice_table,
                      const fvm::VPartitionEntry* vpart_table, fbl::Vector<Slice>* out_slices,
                      fbl::Array<Partition>* out_partitions) const;

  // Displays information about |slices|, assuming they are sorted in physical slice order.
  void DumpSlices(const fbl::Vector<Slice>& slices) const;

  // Confirms the Checker has received necessary arguments before beginning validation.
  bool ValidateOptions() const;

  fbl::unique_fd fd_;
  uint32_t block_size_ = 512;
  Logger logger_;
};

}  // namespace fvm

#endif  // FVM_FVM_CHECK_H_
