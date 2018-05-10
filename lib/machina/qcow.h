// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_QCOW_H_
#define GARNET_LIB_MACHINA_QCOW_H_

#include <endian.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <string>

#include "garnet/lib/machina/block_dispatcher.h"
#include "garnet/lib/machina/qcow_refcount.h"
#include "lib/fxl/macros.h"

namespace machina {

// Each QCOW file starts with this magic value "QFI\xfb".
static constexpr uint32_t kQcowMagic = 0x514649fb;

// The top bits in a table entry have special significance. The remaining bits
// identify the target. For L1 table entries this is physical offset of the L2
// table. For L2 table entries, this is the physical offset of the cluster.
static constexpr uint64_t kTableEntryCopiedBit = 1ul << 63;
static constexpr uint64_t kTableEntryCompressedBit = 1ul << 62;
static constexpr uint64_t kTableOffsetMask =
    ~(kTableEntryCopiedBit | kTableEntryCompressedBit);

struct BigToHostEndianTraits {
  static uint16_t Convert(uint16_t val) { return be16toh(val); }
  static uint32_t Convert(uint32_t val) { return be32toh(val); }
  static uint64_t Convert(uint64_t val) { return be64toh(val); }
};

struct HostToBigEndianTraits {
  static uint16_t Convert(uint16_t val) { return htobe16(val); }
  static uint32_t Convert(uint32_t val) { return htobe32(val); }
  static uint64_t Convert(uint64_t val) { return htobe64(val); }
};

struct QcowHeader {
  uint32_t magic;
  uint32_t version;
  uint64_t backing_file_offset;
  uint32_t backing_file_size;
  uint32_t cluster_bits;
  uint64_t size;
  uint32_t crypt_method;
  uint32_t l1_size;
  uint64_t l1_table_offset;
  uint64_t refcount_table_offset;
  uint32_t refcount_table_clusters;
  uint32_t nb_snapshots;
  uint64_t snapshots_offset;

  // Only present on version 3+
  uint64_t incompatible_features;
  uint64_t compatible_features;
  uint64_t autoclear_features;
  uint32_t refcount_order;
  uint32_t header_length;

  uint32_t cluster_size() const { return 1u << cluster_bits; }

  // Return a new header that has been converted from host-endian to big-endian.
  QcowHeader HostToBigEndian() const {
    return ByteSwap<HostToBigEndianTraits>();
  }

  // Return a new header that has been converted from big-endian to host-endian.
  QcowHeader BigToHostEndian() const {
    return ByteSwap<BigToHostEndianTraits>();
  }

 private:
  // Byte-swaps the members of the header using the desired scheme.
  template <typename ByteOrderTraits>
  QcowHeader ByteSwap() const {
    return QcowHeader{
        .magic = ByteOrderTraits::Convert(magic),
        .version = ByteOrderTraits::Convert(version),
        .backing_file_offset = ByteOrderTraits::Convert(backing_file_offset),
        .backing_file_size = ByteOrderTraits::Convert(backing_file_size),
        .cluster_bits = ByteOrderTraits::Convert(cluster_bits),
        .size = ByteOrderTraits::Convert(size),
        .crypt_method = ByteOrderTraits::Convert(crypt_method),
        .l1_size = ByteOrderTraits::Convert(l1_size),
        .l1_table_offset = ByteOrderTraits::Convert(l1_table_offset),
        .refcount_table_offset =
            ByteOrderTraits::Convert(refcount_table_offset),
        .refcount_table_clusters =
            ByteOrderTraits::Convert(refcount_table_clusters),
        .nb_snapshots = ByteOrderTraits::Convert(nb_snapshots),
        .snapshots_offset = ByteOrderTraits::Convert(snapshots_offset),
        .incompatible_features =
            ByteOrderTraits::Convert(incompatible_features),
        .compatible_features = ByteOrderTraits::Convert(compatible_features),
        .autoclear_features = ByteOrderTraits::Convert(autoclear_features),
        .refcount_order = ByteOrderTraits::Convert(refcount_order),
        .header_length = ByteOrderTraits::Convert(header_length),
    };
  }
} __PACKED;

class QcowFile {
 public:
  QcowFile();
  ~QcowFile();

  QcowFile(QcowFile&& other);
  QcowFile& operator=(QcowFile&& other);

  const QcowHeader& header() const { return header_; }

  // The size (in bytes) of the virtual disk exposed by this QCOW file.
  size_t size() const { return header_.size; }

  // The number of bytes in a single cluster.
  //
  // This will be the unit used for all block allocations (both data and meta-
  // data blocks).
  size_t cluster_size() const { return 1u << header_.cluster_bits; }

  // Load the file header and verifiy the image is a valid QCOW file.
  zx_status_t Load(int fd);

  // Read |size| bytes from the given |linear_offset| in the file.
  //
  // It is not an error for a read to cross an unmapped cluster, but in that
  // cast the section of |buf| that would contain those bytes for the unmapped
  // cluster will be left unmodified.
  zx_status_t Read(uint64_t linear_offset, void* buf, size_t size);

  QcowRefcount* refcount_table() { return &refcount_table_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(QcowFile);

  fbl::unique_fd fd_;
  QcowHeader header_;

  class LookupTable;
  fbl::unique_ptr<LookupTable> lookup_table_;
  QcowRefcount refcount_table_;
};

class QcowDispatcher : public BlockDispatcher {
 public:
  static zx_status_t Create(int fd, bool read_only,
                            fbl::unique_ptr<BlockDispatcher>* out);

 private:
  QcowDispatcher(QcowFile qcow, bool read_only);

  // |BlockDispatcher|
  zx_status_t Flush() override;
  zx_status_t Read(off_t disk_offset, void* buf, size_t size) override;
  zx_status_t Write(off_t disk_offset, const void* buf, size_t size) override;
  zx_status_t Submit() override;

  QcowFile qcow_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_QCOW_H_
