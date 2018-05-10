// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/qcow.h"

#include <fbl/array.h>
#include <fbl/unique_ptr.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "lib/fxl/logging.h"

// Implementation based on the spec located at:
//
// https://github.com/qemu/qemu/blob/27e757e29cc79f3f104d2a84d17cdb3b4c11c8ff/docs/interop/qcow2.txt
namespace machina {

// Compute the number of L1 table entries required to hold all mappings for a
// disk of |disk_size|.
static size_t ComputeL1Size(size_t disk_size, uint32_t cluster_bits) {
  size_t cluster_size = 1 << cluster_bits;
  // Each L2 table is an array of 8b cluster addresses, so each table can hold
  // |cluster_size| / 8 entries.
  size_t l2_num_entries = 1 << (cluster_bits - 3);
  size_t l1_entry_size = (cluster_size * l2_num_entries);
  return (disk_size + l1_entry_size - 1) / l1_entry_size;
}

// A LookupTable holds the 2-level table mapping a linear cluster addres to the
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
  zx_status_t Load(int fd, const QcowHeader& header) {
    if (l1_table_) {
      return ZX_ERR_BAD_STATE;
    }

    uint64_t l1_entries[header.l1_size];
    off_t ret = lseek(fd, header.l1_table_offset, SEEK_SET);
    if (ret < 0) {
      FXL_LOG(ERROR) << "Failed to seek to L1 table: " << ret;
      return ZX_ERR_IO;
    }
    ssize_t result = read(fd, l1_entries, sizeof(l1_entries));
    if (result != static_cast<ssize_t>(sizeof(l1_entries))) {
      FXL_LOG(ERROR) << "Failed to read L1 table: " << result;
      return ZX_ERR_IO;
    }

    size_t l2_size = 1 << (header.cluster_bits - 3);
    l1_table_.reset(new L2Table[header.l1_size], header.l1_size);
    for (size_t l1_entry = 0; l1_entry < header.l1_size; ++l1_entry) {
      uint64_t offset = BigToHostEndianTraits::Convert(l1_entries[l1_entry]) &
                        kTableOffsetMask;
      if (!offset) {
        continue;
      }
      L2Table l2_table(new uint64_t[l2_size], l2_size);
      ret = lseek(fd, offset, SEEK_SET);
      if (ret < 0) {
        FXL_LOG(ERROR) << "Failed to seek to L2 table " << l1_entry << ": "
                       << ret;
        return ZX_ERR_IO;
      }
      // l2_size is number of 8b entries.
      result = read(fd, l2_table.get(), l2_size << 3);
      if (result != static_cast<ssize_t>(l2_size << 3)) {
        FXL_LOG(ERROR) << "Failed to read L2 table " << l1_entry << ": "
                       << result;
        return ZX_ERR_IO;
      }
      l1_table_[l1_entry] = std::move(l2_table);
    }
    return ZX_OK;
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
    if (!l1_table_) {
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
    if (!l2) {
      return ZX_ERR_NOT_FOUND;
    }
    uint64_t l2_entry = BigToHostEndianTraits::Convert(l2[l2_offset]);
    if (l2_entry & kTableEntryCompressedBit) {
      FXL_LOG(ERROR) << "Cluster compression not supported";
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

  using L2Entry = uint64_t;
  using L2Table = fbl::Array<L2Entry>;
  using L1Table = fbl::Array<L2Table>;
  L1Table l1_table_;
};

QcowFile::QcowFile() = default;
QcowFile::~QcowFile() = default;

QcowFile::QcowFile(QcowFile&& other)
    : fd_(std::move(other.fd_)),
      header_(other.header_),
      lookup_table_(std::move(other.lookup_table_)),
      refcount_table_(std::move(other.refcount_table_)) {}

QcowFile& QcowFile::operator=(QcowFile&& other) {
  fd_ = std::move(other.fd_);
  lookup_table_ = std::move(other.lookup_table_);
  refcount_table_ = std::move(other.refcount_table_);
  header_ = other.header_;
  return *this;
}

zx_status_t QcowFile::Load(int fd) {
  fd_.reset(fd);

  // Load QCOW header.
  ssize_t result = -1;
  off_t off = lseek(fd_.get(), 0, SEEK_SET);
  if (off >= 0) {
    result = read(fd_.get(), &header_, sizeof(header_));
  }
  if (result != sizeof(header_)) {
    FXL_LOG(ERROR) << "Unable to read QCOW header";
    return ZX_ERR_WRONG_TYPE;
  }

  header_ = header_.BigToHostEndian();
  if (header_.magic != kQcowMagic) {
    FXL_LOG(ERROR) << "Image file is not a valid QCOW file";
    return ZX_ERR_WRONG_TYPE;
  }

  // Default values for version 2.
  if (header_.version == 2) {
    header_.incompatible_features = 0;
    header_.compatible_features = 0;
    header_.autoclear_features = 0;
    header_.refcount_order = 4;
    header_.header_length = 72;
  } else if (header_.version != 3) {
    FXL_LOG(ERROR) << "QCOW version " << header_.version << " is not supported";
    return ZX_ERR_NOT_SUPPORTED;
  }

  // clang-format off
  FXL_VLOG(1) << "Found QCOW header:";
  FXL_VLOG(1) << "\tmagic:                   0x" << std::hex << header_.magic;
  FXL_VLOG(1) << "\tversion:                 " << std::hex << header_.version;
  FXL_VLOG(1) << "\tbacking_file_offset:     0x" << std::hex << header_.backing_file_offset;
  FXL_VLOG(1) << "\tbacking_file_size:       0x" << std::hex << header_.backing_file_size;
  FXL_VLOG(1) << "\tcluster_bits:            " << header_.cluster_bits;
  FXL_VLOG(1) << "\tsize:                    0x" << std::hex << header_.size;
  FXL_VLOG(1) << "\tcrypt_method:            " << header_.crypt_method;
  FXL_VLOG(1) << "\tl1_size:                 0x" << std::hex << header_.l1_size;
  FXL_VLOG(1) << "\tl1_table_offset:         0x" << std::hex << header_.l1_table_offset;
  FXL_VLOG(1) << "\trefcount_table_offset:   0x" << std::hex << header_.refcount_table_offset;
  FXL_VLOG(1) << "\trefcount_table_clusters: " << header_.refcount_table_clusters;
  FXL_VLOG(1) << "\tnb_snapshots:            " << header_.nb_snapshots;
  FXL_VLOG(1) << "\tsnapshots_offset:        0x" << std::hex << header_.snapshots_offset;
  FXL_VLOG(1) << "\tincompatible_features:   0x" << std::hex << header_.incompatible_features;
  FXL_VLOG(1) << "\tcompatible_features:     0x" << std::hex << header_.compatible_features;
  FXL_VLOG(1) << "\tautoclear_features:      0x" << std::hex << header_.autoclear_features;
  FXL_VLOG(1) << "\trefcount_order:          " << header_.refcount_order;
  FXL_VLOG(1) << "\theader_length:           " << header_.header_length;
  // clang-format on

  // We don't support any optional features so refuse to load an image that
  // requires any.
  if (header_.incompatible_features) {
    FXL_LOG(ERROR) << "Refusing to open QCOW image with incompatible features "
                   << std::hex << "0x" << header_.incompatible_features;
    return ZX_ERR_NOT_SUPPORTED;
  }

  // No encryption is supported.
  if (header_.crypt_method) {
    FXL_LOG(ERROR) << "Refusing to open QCOW image with crypt method "
                   << std::hex << "0x" << header_.crypt_method;
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto lookup_table =
      fbl::make_unique<LookupTable>(header_.cluster_bits, header_.size);
  zx_status_t status = lookup_table->Load(fd_.get(), header_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to load L1 table.";
    return status;
  }

  status = refcount_table_.Load(fd_.get(), header_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to load refcount table.";
    return status;
  }

  lookup_table_ = std::move(lookup_table);
  return ZX_OK;
}

zx_status_t QcowFile::Read(uint64_t disk_offset, void* buf, size_t size) {
  if (!lookup_table_) {
    return ZX_ERR_BAD_STATE;
  }

  uint64_t cluster_mask = cluster_size() - 1;
  while (size) {
    uint64_t physical_offset;
    uint64_t cluster_offset = disk_offset & cluster_mask;
    uint64_t read_size = std::min(size, cluster_size() - cluster_offset);
    zx_status_t status = lookup_table_->Walk(disk_offset, &physical_offset);
    switch (status) {
      case ZX_OK: {
        off_t off = lseek(fd_.get(), physical_offset, SEEK_SET);
        if (off < 0) {
          FXL_LOG(ERROR) << "Failed to seek to 0x" << std::hex
                         << physical_offset;
          return ZX_ERR_IO;
        }
        ssize_t result = read(fd_.get(), buf, read_size);
        if (result != static_cast<ssize_t>(read_size)) {
          FXL_LOG(ERROR) << "Failed to read cluster at 0x" << std::hex
                         << physical_offset;
          return ZX_ERR_IO;
        }
        break;
      }
      case ZX_ERR_NOT_FOUND:
        // Cluster is not mapped; read as zero.
        memset(buf, 0, read_size);
        break;
      default:
        return status;
    }
    size -= read_size;
    disk_offset += read_size;
    buf = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(buf) + read_size);
  }
  return ZX_OK;
}

zx_status_t QcowDispatcher::Create(int fd, bool read_only,
                                   fbl::unique_ptr<BlockDispatcher>* out) {
  if (!read_only) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  QcowFile file = QcowFile();
  zx_status_t status = file.Load(fd);
  if (status != ZX_OK) {
    return status;
  }

  *out = fbl::unique_ptr<QcowDispatcher>(
      new QcowDispatcher(std::move(file), read_only));
  return ZX_OK;
}

QcowDispatcher::QcowDispatcher(QcowFile qcow, bool read_only)
    : BlockDispatcher(qcow.size(), read_only), qcow_(std::move(qcow)) {}

zx_status_t QcowDispatcher::Read(off_t disk_offset, void* buf, size_t size) {
  return qcow_.Read(disk_offset, buf, size);
}

zx_status_t QcowDispatcher::Write(off_t disk_offset, const void* buf,
                                  size_t size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t QcowDispatcher::Submit() { return ZX_OK; }

zx_status_t QcowDispatcher::Flush() { return ZX_OK; }

}  //  namespace machina
