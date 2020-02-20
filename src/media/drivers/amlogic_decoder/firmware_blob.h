// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_FIRMWARE_BLOB_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_FIRMWARE_BLOB_H_

#include <lib/zx/vmo.h>

#include <map>
#include <string>

#include <ddk/device.h>
#include <ddk/driver.h>

class FirmwareBlob {
 public:
  // Some of these values are used in communicating with the TEE to switch firmware; these values
  // must not change (other than possibly adding more at the end as appropriate).  These values are
  // for the first parameter to the SMC call that switches firmware via the TEE.
  enum class FirmwareType {
    // The driver code internally has some limited partial experimental support for Mpeg2, but it's
    // not exposed / accessible outside the driver.
    kDec_Mpeg12 = 0,

    // These are not used so far:
    kDec_Mpeg4_3 = 1,
    kDec_Mpeg4_4 = 2,
    kDec_Mpeg4_5 = 3,
    kDec_H263 = 4,
    kDec_Mjpeg = 5,
    kDec_Mjpeg_Multi = 6,
    kDec_Real_v8 = 7,
    kDec_Real_v9 = 8,
    kDec_Vc1 = 9,
    kDec_Avs = 10,

    // Used by this driver:
    kDec_H264 = 11,

    // These are not used so far:
    kDec_H264_4k2k = 12,
    kDec_H264_4k2k_Single = 13,
    kDec_H264_Mvc = 14,
    kDec_H264_Multi = 15,
    kDec_Hevc = 16,
    kDec_Hevc_Mmu = 17,
    kDec_Vp9 = 18,

    // Used by this driver:
    kDec_Vp9_Mmu = 19,

    // These are not used so far:
    kEnc_H264 = 20,
    kEnc_Jpeg = 21,
    // kPackage = 22,  // not a firmware
    kDec_H264_Multi_Mmu = 23,
    kDec_Hevc_G12a = 24,

    // Used by this driver:
    kDec_Vp9_G12a = 25,

    // These are not used so far:
    kDec_Avs2 = 26,
    kDec_Avs2_Mmu = 27,
    kDec_Avs_Gxm = 28,
    kDec_Avs_NoCabac = 29,
    kDec_H264_Multi_Gxm = 30,
    kDec_H264_Mvc_Gxm = 31,
    kDec_Vc1_G12a = 32,

    kCount = 33,  // count of above packed values
  };

  // The SMC call to switch firmware via the TEE takes a second parameter that controls how the
  // firmware is specified to the HW by the TEE.
  enum class FirmwareVdecLoadMode {
    kCompatible = 0,  // Used by h264 decoder.
    kLegacy = 1,      // Not used so far.  Spelled "legency" in some other places.
    kHevc = 2,        // Used by VP9 decoder.
  };

  ~FirmwareBlob();

  zx_status_t LoadFirmware(zx_device_t* device);

  zx_status_t GetFirmwareData(FirmwareType firmware_type, uint8_t** data_out, uint32_t* size_out);

  // When pre-loading the firmware via video_firmware TA, we need the whole blob.
  //
  // Requires: LoadFirmware() succeeded.
  void GetWholeBlob(uint8_t** data_out, uint32_t* size_out);

  void LoadFakeFirmwareForTesting(FirmwareType firmware_type, uint8_t* data, uint32_t size);

 private:
  struct FirmwareCode {
    uint64_t offset;
    uint32_t size;
  };

  zx::vmo vmo_;
  uintptr_t ptr_ = 0;
  uint64_t fw_size_ = 0;
  std::map<std::string, FirmwareCode> firmware_code_;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_FIRMWARE_BLOB_H_
