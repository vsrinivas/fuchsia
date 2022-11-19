// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FVM_FVM_CHECK_H_
#define SRC_STORAGE_FVM_FVM_CHECK_H_

#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <stdarg.h>
#include <stdlib.h>

#include <utility>

#include <fbl/array.h>
#include <fbl/vector.h>

#include "src/storage/fvm/format.h"

namespace fvm {

// Checker defines a class which may be used to validate an FVM
// (provided as either a regular file or a raw block device).
class Checker {
 public:
  Checker(fidl::UnownedClientEnd<fuchsia_hardware_block::Block> block, uint32_t block_size,
          bool silent);
  Checker(fidl::UnownedClientEnd<fuchsia_io::File> file, uint32_t block_size, bool silent);

  // Read from and validate the provided device, logging information if requested.
  bool Validate() const;

 private:
  class Logger {
   public:
    explicit Logger(bool silent) : silent_(silent) {}

    // Prints the format string and arguments to stderr.
    static void Error(const char* format, ...) {
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
    const bool silent_;
  };

  class Interface {
   public:
    virtual ~Interface() = default;

    virtual zx::result<size_t> Size() const = 0;
    virtual zx::result<size_t> Read(void* buf, size_t count) const = 0;
  };

  Checker(std::unique_ptr<Interface> interface, uint32_t block_size, bool silent);

  class Block : public Interface {
   public:
    explicit Block(fidl::UnownedClientEnd<fuchsia_hardware_block::Block> block);
    ~Block() override;

   private:
    zx::result<size_t> Size() const override;
    zx::result<size_t> Read(void* buf, size_t count) const override;

    const fidl::UnownedClientEnd<fuchsia_hardware_block::Block> block_;
  };

  class File : public Interface {
   public:
    explicit File(fidl::UnownedClientEnd<fuchsia_io::File> file);
    ~File() override;

   private:
    zx::result<size_t> Size() const override;
    zx::result<size_t> Read(void* buf, size_t count) const override;

    const fidl::UnownedClientEnd<fuchsia_io::File> file_;
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
  bool LoadPartitions(size_t slice_count, const fvm::SliceEntry* slice_table,
                      const fvm::VPartitionEntry* vpart_table, fbl::Vector<Slice>* out_slices,
                      fbl::Array<Partition>* out_partitions) const;

  // Displays information about |slices|, assuming they are sorted in physical slice order.
  void DumpSlices(const fbl::Vector<Slice>& slices) const;

  // Confirms the Checker has received necessary arguments before beginning validation.
  bool ValidateOptions() const;

  const std::unique_ptr<Interface> interface_;
  const uint32_t block_size_;
  const Logger logger_;
};

}  // namespace fvm

#endif  // SRC_STORAGE_FVM_FVM_CHECK_H_
