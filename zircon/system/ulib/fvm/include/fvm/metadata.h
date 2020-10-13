// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FVM_METADATA_H_
#define FVM_METADATA_H_

#include <lib/zx/status.h>
#include <zircon/types.h>

#include <fbl/span.h>
#include <fvm/format.h>

namespace fvm {

// MetadataBuffer is an interface for a buffer that contains FVM metadata.
class MetadataBuffer {
 public:
  virtual ~MetadataBuffer() = default;
  // Returns the minimum number of bytes needed for a |MetadataBuffer| object to back FVM metadata
  // described by |header|.
  static size_t BytesNeeded(const Header& header);

  // Creates an uninitialized |MetadataBuffer| which has capacity for |size| bytes.
  // This is intentionally non-static so inheriting classes can override it to return the
  // appropriate type. In general the instance's fields/methods will not be accessed.
  virtual std::unique_ptr<MetadataBuffer> Create(size_t size) const = 0;

  virtual void* data() const = 0;
  virtual size_t size() const = 0;
};

// HeapMetadataBuffer is an instance of |MetadataBuffer| backed by a heap-allocated buffer.
class HeapMetadataBuffer : public MetadataBuffer {
 public:
  HeapMetadataBuffer(std::unique_ptr<uint8_t[]> buffer, size_t size);
  ~HeapMetadataBuffer() override;

  std::unique_ptr<MetadataBuffer> Create(size_t size) const override;

  void* data() const override { return buffer_.get(); };
  size_t size() const override { return size_; };

 private:
  std::unique_ptr<uint8_t[]> buffer_;
  size_t size_;
};

// Metadata is an in-memory representation of the metadata for an FVM image.
//
// At construction, |Metadata| objects are well-formed, since they validate the underlying metadata
// when first created by |Metadata::Create|. Subsequent updates by clients can, of course, corrupt
// the metadata.
//
// This class owns the underlying buffer (see |MetadataBuffer)|.
//
// This class is not thread-safe.
class Metadata {
 public:
  // Constructs a default, uninitialized instance.
  Metadata() = default;
  Metadata(const Metadata&) = delete;
  Metadata& operator=(const Metadata&) = delete;
  Metadata(Metadata&&) noexcept;
  Metadata& operator=(Metadata&&) noexcept;

  // Attempts to parse the FVM metadata stored at |data|.
  // Returns a |Metadata| instance over the passed buffer on success, or a failure if the buffer was
  // not parseable.
  static zx::status<Metadata> Create(std::unique_ptr<MetadataBuffer> data);

  // Creates an instance of |Metadata|, initialized by copying the contents of |header|,
  // |partitions| and |slices|.
  // All of the passed metadata is copied into both the A and B slots. Any additional partitions
  // and slices in the tables past |partitions| and |slices| are default-initialized.
  // The passed |header| must be configured appropriately to manage tables at least as big as
  // |num_partitions| and |num_slices| respectively. If not, an error is returned.
  static zx::status<Metadata> Synthesize(const fvm::Header& header,
                                         const VPartitionEntry* partitions, size_t num_partitions,
                                         const SliceEntry* slices, size_t num_slices);

  // Checks the validity of the metadata.
  // Should be called before serializing the contents to disk.
  bool CheckValidity() const;

  // Updates the hashes stored in the metadata, based on its contents.
  void UpdateHash();

  // Returns which of the A/B copies is active.
  // Generally, the active copy should *NOT* be written to.
  SuperblockType active_header() const { return active_header_; }

  // Accesses the headers managed by the Metadata instance.
  Header& GetHeader(SuperblockType type) const;

  // Accesses the partition tables. Note that |idx| is one-based.
  VPartitionEntry& GetPartitionEntry(SuperblockType type, unsigned idx) const;

  // Accesses the allocation tables. Note that |idx| is one-based.
  SliceEntry& GetSliceEntry(SuperblockType type, unsigned idx) const;

  // Gets a view of the raw metadata buffer. Should not be widely used, as the layout has no
  // guarantees. Example uses are copying the metadata in bulk without interpreting it.
  const MetadataBuffer* UnsafeGetRaw() const;

  // Creates a copy of this Metadata instance, with additional room described by |dimensions|.
  // The metadata is not copied verbatim; for instance, which of the A/B copies is active
  // may change, and old generations may be lost. The only guarantee is that all partition/slice
  // entries in the active tables will be copied over from this instance.
  // TODO(jfsulliv): Only fvm-host needs this method, and it is not very graceful. Consider removal.
  zx::status<Metadata> CopyWithNewDimensions(const Header& dimensions) const;

 private:
  Metadata(std::unique_ptr<MetadataBuffer> data, SuperblockType active_header);

  void MoveFrom(Metadata&& o);

  size_t MetadataOffset(SuperblockType type) const;

  std::unique_ptr<MetadataBuffer> data_;
  SuperblockType active_header_{SuperblockType::kPrimary};
};

}  // namespace fvm

#endif  // FVM_METADATA_H_
