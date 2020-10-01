// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_VOLUME_H_
#define LIB_FTL_VOLUME_H_

#include <lib/ftl/ndm-driver.h>
#include <zircon/types.h>

#include <cstdint>
#include <memory>

#include <fbl/macros.h>

struct XfsVol;

namespace ftl {

// Interface for an upper-layer (block device) view of an FTL.
class __EXPORT FtlInstance {
 public:
  virtual ~FtlInstance() {}

  // Notified when an FTL volume is created. A block device can be created
  // with up to num_pages blocks of page_size bytes each. The implementation
  // should return true to acknowledge the success of the operation.
  virtual bool OnVolumeAdded(uint32_t page_size, uint32_t num_pages) = 0;
};

// Exposes the upper layer (block device) interface of the FTL.
class __EXPORT Volume {
 public:
  // Basic stats about the state of the device.
  struct Stats {
    size_t ram_used;
    uint32_t wear_count;

    // Histogram of the wear level distribution. Each bucket represents about 5%
    // of the valid range, with the first bucket storing the number of blocks
    // with the lowest wear count, and the last bucket the most reused blocks.
    // If all blocks have the same wear count, the first 19 buckets will have no
    // samples.
    uint32_t wear_histogram[20];
    uint32_t num_blocks;
    int garbage_level;  // Percentage of free space that can be garbage-collected.
  };

  struct Counters {
    uint32_t wear_count = 0;
  };

  Volume() {}
  virtual ~Volume() {}

  // Performs the object initialization. Returns an error string, or nullptr
  // on success. Will synchronously call FtlInstance::OnVolumeAdded on success.
  // The |driver| must be fully initialized when passed to this method.
  virtual const char* Init(std::unique_ptr<NdmDriver> driver) = 0;

  // Removes the volume and re-attaches to it. This is roughly equivalent to
  // what a shutdown / restart would to in the real world (this functionality
  // is basically intended for testing). Returns an error string, or nullptr
  // on success. Will synchronously call FtlInstance::OnVolumeAdded on success.
  virtual const char* ReAttach() = 0;

  // Synchronously Read or Write num_pages starting at first_page.
  virtual zx_status_t Read(uint32_t first_page, int num_pages, void* buffer) = 0;
  virtual zx_status_t Write(uint32_t first_page, int num_pages, const void* buffer) = 0;

  // Issues a command to format the FTL (aka, delete all data).
  virtual zx_status_t Format() = 0;

  // Issues a command to format the FTL (aka, delete all data), and also treat
  // all blocks as equally leveled (same number of erase cycles). Use this with
  // caution as losing wear leveling information is normally a bad thing.
  virtual zx_status_t FormatAndLevel() = 0;

  // Marks the volume as in use (Mount) or not (Unmount).
  virtual zx_status_t Mount() = 0;
  virtual zx_status_t Unmount() = 0;

  // Flushes all data to the device.
  virtual zx_status_t Flush() = 0;

  // Marks num_pages starting from first_page as not-needed.
  virtual zx_status_t Trim(uint32_t first_page, uint32_t num_pages) = 0;

  // Goes through one cycle of synchronous garbage collection. Returns ZX_OK
  // on success and ZX_ERR_STOP where there is no more work to do.
  virtual zx_status_t GarbageCollect() = 0;

  // Returns basic stats about the device.
  virtual zx_status_t GetStats(Stats* stats) = 0;

  // Returns basic counters about the device.
  virtual zx_status_t GetCounters(Counters* stats) = 0;
};

// Implementation of the Volume interface.
class __EXPORT VolumeImpl final : public Volume {
 public:
  VolumeImpl(FtlInstance* owner) : owner_(owner) {}
  ~VolumeImpl() final {}

  // Volume interface.
  const char* Init(std::unique_ptr<NdmDriver> driver) final;
  const char* ReAttach() final;
  zx_status_t Read(uint32_t first_page, int num_pages, void* buffer) final;
  zx_status_t Write(uint32_t first_page, int num_pages, const void* buffer) final;
  zx_status_t Format() final;
  zx_status_t FormatAndLevel() final;
  zx_status_t Mount() final;
  zx_status_t Unmount() final;
  zx_status_t Flush() final;
  zx_status_t Trim(uint32_t first_page, uint32_t num_pages) final;
  zx_status_t GarbageCollect() final;
  zx_status_t GetStats(Stats* stats) final;
  zx_status_t GetCounters(Counters* counters) final;

  // Internal notification of added volumes. This is forwarded to
  // FtlInstance::OnVolumeAdded.
  bool OnVolumeAdded(const XfsVol* ftl);

  DISALLOW_COPY_ASSIGN_AND_MOVE(VolumeImpl);

 private:
  // Returns true if the volume was created successfully.
  bool Created() const;

  // Creates the underlying NDM volume and mounts it. If successful, |owner_|
  // will be notified about the new volume inside this call.
  const char* Attach();

  // Members that are initialized when the volume is created (OnVolumeAdded):
  void* vol_ = nullptr;         // FTL volume handle for callbacks.
  const char* name_ = nullptr;  // Volume name from driver.
  int (*report_)(void* vol, uint32_t msg, ...) = nullptr;
  int (*write_pages_)(const void* buffer, uint32_t first_page, int count, void* vol) = nullptr;
  int (*read_pages_)(void* buffer, uint32_t first_page, int count, void* vol) = nullptr;

  FtlInstance* owner_;
  std::unique_ptr<NdmDriver> driver_;
};

}  // namespace ftl

#endif  // LIB_FTL_VOLUME_H_
