// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/qcow.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/lib/syslog/cpp/logger.h"

// Implementation based on the spec located at:
//
// https://github.com/qemu/qemu/blob/master/docs/interop/qcow2.txt

// Compute the number of L1 table entries required to hold all mappings for a
// disk of |disk_size|.
static size_t ComputeL1Size(size_t disk_size, uint32_t cluster_bits) {
  size_t cluster_size = 1 << cluster_bits;
  // Each L2 table is an array of 8b cluster addresses, so each table can hold
  // |cluster_size| / 8 entries.
  size_t l2_num_entries = cluster_size / sizeof(uint64_t);
  size_t l1_entry_size = cluster_size * l2_num_entries;
  return (disk_size + l1_entry_size - 1) / l1_entry_size;
}

// A LookupTable holds the 2-level table mapping a linear cluster address to the
// physical offset in the QCOW file.
class QcowFile::LookupTable {
 public:
  LookupTable(uint32_t cluster_bits, size_t disk_size)
      : cluster_bits_(cluster_bits),
        l2_bits_(cluster_bits - 3),
        l1_size_(ComputeL1Size(disk_size, cluster_bits)) {}

  // Loads the L1 table to use for cluster mapping.
  //
  // Note we currently load all existing L2 tables for the disk so that all
  // mappings are held in memory. With a 64k cluster size this results in 1MB
  // of tables per 8GB of virtual disk.
  //
  // TODO(tjdetwiler): Add some bound to this L2 cache.
  void Load(const QcowHeader& header, BlockDispatcher* disp, BlockDispatcher::Callback callback) {
    if (!l1_table_.empty()) {
      callback(ZX_ERR_BAD_STATE);
      return;
    }

    auto io_guard = fbl::MakeRefCounted<IoGuard>(std::move(callback));
    uint32_t l1_size = header.l1_size;
    if (l1_size < l1_size_) {
      FX_LOGS(ERROR) << "Invalid QCOW header: L1 table is too small. Image size requires "
                     << l1_size_ << " entries but the header specifies " << l1_size << ".";
      io_guard->SetStatus(ZX_ERR_INVALID_ARGS);
      return;
    }
    std::vector<L1Entry> l1_entries(l1_size);
    auto l1_entries_ptr = l1_entries.data();
    size_t l2_size = 1 << (header.cluster_bits - 3);

    auto load_l1 = [this, io_guard, l1_entries = std::move(l1_entries), l2_size,
                    disp](zx_status_t status) {
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to read L1 table " << status;
        io_guard->SetStatus(status);
        return;
      }

      l1_table_.resize(l1_entries.size());
      for (size_t l1_index = 0; l1_index < l1_entries.size(); ++l1_index) {
        uint64_t off = BigToHostEndianTraits::Convert(l1_entries[l1_index]) & kTableOffsetMask;
        if (off == 0) {
          continue;
        }
        auto load_l2 = [io_guard](zx_status_t status) {
          if (status != ZX_OK) {
            FX_LOGS(ERROR) << "Failed to read L2 table " << status;
            io_guard->SetStatus(status);
          }
        };
        auto& l2_table = l1_table_[l1_index];
        l2_table.resize(l2_size);
        disp->ReadAt(l2_table.data(), l2_size * sizeof(L2Entry), off, load_l2);
      }
    };
    disp->ReadAt(l1_entries_ptr, l1_size * sizeof(L1Entry), header.l1_table_offset,
                 std::move(load_l1));
  }

  // Walks the tables to find the physical offset of |linear_offset| into
  // the image file. The returned value is only valid up until the next cluster
  // boundary.
  //
  // Returns:
  //  |ZX_OK| - The lineary address is mapped and the physical offset into the
  //      QCOW file is written to |physical_offset|.
  //  |ZX_ERR_NOT_FOUND| - The linear offset is valid, but the cluster is not
  //      mapped.
  //  |ZX_ERR_OUT_OF_RANGE| - The linear offset is outside the bounds of the
  //      virtual disk.
  //  |ZX_ERR_NOT_SUPPORTED| - The cluster is compressed.
  //  |ZX_ERR_BAD_STATE| - The file has not yet been initialized with a call to
  //      |Load|.
  zx_status_t Walk(size_t linear_offset, uint64_t* physical_offset) {
    if (l1_table_.empty()) {
      return ZX_ERR_BAD_STATE;
    }

    size_t cluster_offset = linear_offset & (1 << cluster_bits_) - 1;
    linear_offset >>= cluster_bits_;
    size_t l2_offset = linear_offset & (1 << l2_bits_) - 1;
    linear_offset >>= l2_bits_;
    size_t l1_offset = linear_offset;
    if (l1_offset >= l1_size_) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    const auto& l2 = l1_table_[l1_offset];
    if (l2.empty()) {
      return ZX_ERR_NOT_FOUND;
    }
    uint64_t l2_entry = BigToHostEndianTraits::Convert(l2[l2_offset]);
    if (l2_entry & kTableEntryCompressedBit) {
      FX_LOGS(ERROR) << "Cluster compression not supported";
      return ZX_ERR_NOT_SUPPORTED;
    }
    uint64_t cluster = l2_entry & kTableOffsetMask;
    if (cluster == 0) {
      return ZX_ERR_NOT_FOUND;
    }
    *physical_offset = cluster | cluster_offset;
    return ZX_OK;
  }

 private:
  size_t cluster_bits_;
  size_t l2_bits_;
  size_t l1_size_;

  using L1Entry = uint64_t;
  using L2Entry = uint64_t;
  using L2Table = std::vector<L2Entry>;
  using L1Table = std::vector<L2Table>;
  L1Table l1_table_;
};

QcowFile::QcowFile() = default;
QcowFile::~QcowFile() = default;

void QcowFile::Load(BlockDispatcher* disp, BlockDispatcher::Callback callback) {
  auto load = [this, disp, callback = std::move(callback)](zx_status_t status) mutable {
    // Load QCOW header.
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to read QCOW header";
      callback(ZX_ERR_WRONG_TYPE);
      return;
    }
    header_ = header_.BigToHostEndian();
    LoadLookupTable(disp, std::move(callback));
  };
  disp->ReadAt(&header_, sizeof(header_), 0, std::move(load));
}

void QcowFile::LoadLookupTable(BlockDispatcher* disp, BlockDispatcher::Callback callback) {
  if (header_.magic != kQcowMagic) {
    FX_LOGS(ERROR) << "Invalid QCOW image";
    callback(ZX_ERR_WRONG_TYPE);
    return;
  }
  // Default values for version 2.
  if (header_.version == 2) {
    header_.incompatible_features = 0;
    header_.compatible_features = 0;
    header_.autoclear_features = 0;
    header_.refcount_order = 4;
    header_.header_length = 72;
  } else if (header_.version != 3) {
    FX_LOGS(ERROR) << "QCOW version " << header_.version << " is not supported";
    callback(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  // We don't support any optional features so refuse to load an image that
  // requires any.
  if (header_.incompatible_features) {
    FX_LOGS(ERROR) << "Rejecting QCOW image with incompatible features " << std::hex << "0x"
                   << header_.incompatible_features;
    callback(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  // No encryption is supported.
  if (header_.crypt_method) {
    FX_LOGS(ERROR) << "Rejecting QCOW image with crypt method " << std::hex << "0x"
                   << header_.crypt_method;
    callback(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  // clang-format off
  FX_VLOGS(1) << "Found QCOW header:";
  FX_VLOGS(1) << "\tmagic:                   0x" << std::hex << header_.magic;
  FX_VLOGS(1) << "\tversion:                 " << std::hex << header_.version;
  FX_VLOGS(1) << "\tbacking_file_offset:     0x" << std::hex << header_.backing_file_offset;
  FX_VLOGS(1) << "\tbacking_file_size:       0x" << std::hex << header_.backing_file_size;
  FX_VLOGS(1) << "\tcluster_bits:            " << header_.cluster_bits;
  FX_VLOGS(1) << "\tsize:                    0x" << std::hex << header_.size;
  FX_VLOGS(1) << "\tcrypt_method:            " << header_.crypt_method;
  FX_VLOGS(1) << "\tl1_size:                 0x" << std::hex << header_.l1_size;
  FX_VLOGS(1) << "\tl1_table_offset:         0x" << std::hex << header_.l1_table_offset;
  FX_VLOGS(1) << "\trefcount_table_offset:   0x" << std::hex << header_.refcount_table_offset;
  FX_VLOGS(1) << "\trefcount_table_clusters: " << header_.refcount_table_clusters;
  FX_VLOGS(1) << "\tnb_snapshots:            " << header_.nb_snapshots;
  FX_VLOGS(1) << "\tsnapshots_offset:        0x" << std::hex << header_.snapshots_offset;
  FX_VLOGS(1) << "\tincompatible_features:   0x" << std::hex << header_.incompatible_features;
  FX_VLOGS(1) << "\tcompatible_features:     0x" << std::hex << header_.compatible_features;
  FX_VLOGS(1) << "\tautoclear_features:      0x" << std::hex << header_.autoclear_features;
  FX_VLOGS(1) << "\trefcount_order:          " << header_.refcount_order;
  FX_VLOGS(1) << "\theader_length:           " << header_.header_length;
  // clang-format on

  lookup_table_ = std::make_unique<LookupTable>(header_.cluster_bits, header_.size);
  lookup_table_->Load(header_, disp, std::move(callback));
};

void QcowFile::ReadAt(BlockDispatcher* disp, void* data, uint64_t size, uint64_t off,
                      BlockDispatcher::Callback callback) {
  if (!lookup_table_) {
    callback(ZX_ERR_BAD_STATE);
    return;
  }

  auto io_guard = fbl::MakeRefCounted<IoGuard>(std::move(callback));
  auto addr = static_cast<uint8_t*>(data);
  uint64_t cluster_mask = cluster_size() - 1;
  while (size) {
    uint64_t physical_offset;
    uint64_t cluster_offset = off & cluster_mask;
    uint64_t read_size = std::min(size, cluster_size() - cluster_offset);
    zx_status_t status = lookup_table_->Walk(off, &physical_offset);
    switch (status) {
      case ZX_OK: {
        auto load = [io_guard](zx_status_t status) {
          if (status != ZX_OK) {
            io_guard->SetStatus(status);
          }
        };
        disp->ReadAt(addr, read_size, physical_offset, load);
        break;
      }
      case ZX_ERR_NOT_FOUND:
        // Cluster is not mapped; read as zero.
        memset(addr, 0, read_size);
        break;
      default:
        io_guard->SetStatus(status);
        return;
    }

    off += read_size;
    addr += read_size;
    size -= read_size;
  }
}
