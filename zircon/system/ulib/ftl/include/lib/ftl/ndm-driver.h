// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_NDM_DRIVER_H_
#define LIB_FTL_NDM_DRIVER_H_

#include <zircon/compiler.h>

#include <cstdint>
#include <optional>

struct ndm;
struct NDMDrvr;

namespace ftl {

class Volume;

// Return values expected by NDM from the nand driver.
// See ndm-man-20150.pdf for the complete low level API specification:
constexpr int kNdmOk = 0;
constexpr int kNdmError = -1;
constexpr int kNdmUncorrectableEcc = -1;
constexpr int kNdmFatalError = -2;
constexpr int kNdmUnsafeEcc = 1;
constexpr int kTrue = 1;
constexpr int kFalse = 0;

// Initialization should not alter the contents of the volume.
constexpr uint32_t kReadOnlyInit = (1 << 8);  // Matches FSF_READ_ONLY_INIT.

// Options for a device to be created. All sizes are in bytes.
struct VolumeOptions {
  uint32_t num_blocks;
  uint32_t max_bad_blocks;
  uint32_t block_size;
  uint32_t page_size;
  uint32_t eb_size;  // Extra bytes, a.k.a. OOB.
  uint32_t flags;
};

// Helper for overriding default logging routines.
struct LoggerProxy {
  __PRINTFLIKE(1, 2) void (*trace)(const char*, ...) = nullptr;
  __PRINTFLIKE(1, 2) void (*debug)(const char*, ...) = nullptr;
  __PRINTFLIKE(1, 2) void (*info)(const char*, ...) = nullptr;
  __PRINTFLIKE(1, 2) void (*warn)(const char*, ...) = nullptr;
  __PRINTFLIKE(1, 2) void (*error)(const char*, ...) = nullptr;
};

// Encapsulates the lower layer TargetFtl-Ndm driver.
class __EXPORT NdmDriver {
 public:
  virtual ~NdmDriver() {}

  // Performs driver initialization. Returns an error string, or nullptr on
  // success.
  virtual const char* Init() = 0;

  // Creates a new volume. Note that multiple volumes are not supported.
  // |ftl_volume| (if provided) will be notified with the volume details.
  // Returns an error string, or nullptr on success.
  virtual const char* Attach(const Volume* ftl_volume) = 0;

  // Destroy the volume created with Attach(). Returns true on success.
  virtual bool Detach() = 0;

  // Reads |page_count| pages starting at |start_page|, placing the results on
  // |page_buffer| and |oob_buffer|. Either pointer can be nullptr if that
  // part is not desired.
  // Returns kNdmOk, kNdmUncorrectableEcc, kNdmFatalError or kNdmUnsafeEcc.
  virtual int NandRead(uint32_t start_page, uint32_t page_count, void* page_buffer,
                       void* oob_buffer) = 0;

  // Writes |page_count| pages starting at |start_page|, using the data from
  // |page_buffer| and |oob_buffer|.
  // Returns kNdmOk, kNdmError or kNdmFatalError. kNdmError triggers marking
  // the block as bad.
  virtual int NandWrite(uint32_t start_page, uint32_t page_count, const void* page_buffer,
                        const void* oob_buffer) = 0;

  // Erases the block containing |page_num|.
  // Returns kNdmOk or kNdmError. kNdmError triggers marking the block as bad.
  virtual int NandErase(uint32_t page_num) = 0;

  // Returns whether the block containing |page_num| was factory-marked as bad.
  // Returns kTrue, kFalse or kNdmError.
  virtual int IsBadBlock(uint32_t page_num) = 0;

  // Returns whether a given page is empty or not. |data| and |spare| store
  // the contents of the page.
  virtual bool IsEmptyPage(uint32_t page_num, const uint8_t* data, const uint8_t* spare) = 0;
};

// Base functionality for a driver implementation.
class __EXPORT NdmBaseDriver : public NdmDriver {
 public:
  NdmBaseDriver() {}
  virtual ~NdmBaseDriver();

  // Returns true if known data appears to be present on the device. This does
  // not imply that creating a volume will not find errors, just that calling
  // CreateNdmVolume after this method returns false will result in a freshly
  // minted (aka empty) volume.
  // This method should be called after Init(), but before CreateNdmVolume(),
  // for the result to be meaningful, but calling this is not required.
  // |use_format_v2| tells NDM to use the latest file format for the volume, if
  // a new volume is eventually created.
  bool IsNdmDataPresent(const VolumeOptions& options, bool use_format_v2 = true);

  // Returns true if the size of the bad block reservation cannot be used.
  // The size to use (options.max_bad_blocks) may be too small to hold the
  // current known bad blocks, or some internal value may be inconsistent if
  // this size is used.
  // This only makes sense when comparing the desired options with the data
  // already stored on a volume, and in general should only be used to attempt
  // to reduce the reserved space (increasing it would reduce the size of the
  // visible volume, and that is not supported).
  // This method should only be called right after calling IsNdmDataPresent(),
  // before calling CreateNdmVolume().
  bool BadBbtReservation() const;

  // Creates the underlying NDM volume, with the provided parameters. Setting
  // |save_volume_data| to true enables writing of NDM control data version 2.
  const char* CreateNdmVolume(const Volume* ftl_volume, const VolumeOptions& options,
                              bool save_volume_data = true);

  // Just like |CreateNdmVolume| but provides an override for default logging routines.
  const char* CreateNdmVolumeWithLogger(const Volume* ftl_volume, const VolumeOptions& options,
                                        bool save_volume_data, std::optional<LoggerProxy> logger);

  // Deletes the underlying NDM volume.
  bool RemoveNdmVolume();

  // Saves and restores bad block data for volume extension.
  bool SaveBadBlockData();
  bool RestoreBadBlockData();

  // Inspects |data_len| bytes from |data| and |spare_len| bytes from |spare|
  // looking for a typical empty (erased) page. Returns true if all bits are 1.
  bool IsEmptyPageImpl(const uint8_t* data, uint32_t data_len, const uint8_t* spare,
                       uint32_t spare_len) const;

  // Returns the settings used for the volume. The NDM volume has to be created
  // with |save_volume_data| set to true.
  const VolumeOptions* GetSavedOptions() const;

  // Returns true when volume data is saved on disk, either from a previous run
  // or written by this run.
  bool volume_data_saved() const { return volume_data_saved_; }

  // Writes volume information to storage. Returns true on success.
  // This should only be called after a successful call to CreateNdmVolume with
  // save_volume_data set to true.
  bool WriteVolumeData();

 protected:
  // This is exposed for unit tests only.
  ndm* GetNdmForTest() const { return ndm_; }

  // This is exposed for unit tests only.
  void FillNdmDriver(const VolumeOptions& options, bool use_format_v2, NDMDrvr* driver) const;

 private:
  ndm* ndm_ = nullptr;
  bool volume_data_saved_ = false;
  std::optional<LoggerProxy> logger_ = std::nullopt;
};

// Performs global module initialization. This is exposed to support unit tests,
// and while calling this function multiple times is supported, racing calls are
// not (or more generally, calling this from multiple threads).
// If multiple simultaneous tests from the same test instance ever becomes a
// thing, this should be called from testing::Environment and not from each
// test case.
bool InitModules();

}  // namespace ftl

#endif  // LIB_FTL_NDM_DRIVER_H_
