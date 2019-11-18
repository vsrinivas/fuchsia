// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "firmware_blob.h"

#include <ddk/debug.h>
#include <lib/zx/vmar.h>
#include <zircon/assert.h>

#include "macros.h"

FirmwareBlob::~FirmwareBlob() {
  if (vmo_)
    zx::vmar::root_self()->unmap(ptr_, fw_size_);
}

zx_status_t FirmwareBlob::LoadFirmware(zx_device_t* device) {
  zx_status_t status =
      load_firmware(device, "amlogic_video_ucode.bin", vmo_.reset_and_get_address(), &fw_size_);
  if (status != ZX_OK) {
    DECODE_ERROR("Couldn't load amlogic firmware\n");
    return status;
  }

  zx::vmar::root_self()->map(0, vmo_, 0, fw_size_, ZX_VM_PERM_READ, &ptr_);
  enum {
    kSignatureSize = 256,
    kPackageHeaderSize = 256,
  };
  uint8_t* data = reinterpret_cast<uint8_t*>(ptr_);
  struct PackageEntryHeader {
    union {
      struct {
        char name[32];
        char format[32];
        char cpu[32];
        uint32_t length;
      } data;
      uint8_t data_bytes[256];
    };
  };

  struct FirmwareHeader {
    union {
      struct {
        uint32_t magic;
        uint32_t checksum;
        uint8_t name[32];
        uint8_t cpu[16];
        uint8_t format[32];
        uint8_t version[32];
        uint8_t author[32];
        uint8_t date[32];
        uint8_t commit[16];
        uint32_t data_size;
        uint8_t time;
      } data;
      uint8_t data_bytes[512];
    };
  };
  uint64_t offset = kSignatureSize + kPackageHeaderSize;
  while (offset < fw_size_) {
    if (offset + sizeof(PackageEntryHeader) > fw_size_) {
      DECODE_ERROR("PackageHeader doesn't fit in data\n");
      return ZX_ERR_NO_MEMORY;
    }

    auto header = reinterpret_cast<PackageEntryHeader*>(data + offset);

    offset += sizeof(PackageEntryHeader);
    uint32_t package_length = header->data.length;
    if (offset + package_length > fw_size_) {
      DECODE_ERROR("Package too long\n");
      return ZX_ERR_NO_MEMORY;
    }
    if (sizeof(FirmwareHeader) > package_length) {
      DECODE_ERROR("FirmwareHeader doesn't fit in data %d\n", package_length);
      return ZX_ERR_NO_MEMORY;
    }

    FirmwareHeader* firmware_data = reinterpret_cast<FirmwareHeader*>(data + offset);
    uint32_t firmware_length = firmware_data->data.data_size;
    if (static_cast<uint64_t>(firmware_length) + sizeof(FirmwareHeader) > package_length) {
      DECODE_ERROR("Firmware data doesn't fit in data %d %ld %d\n", firmware_length,
                   sizeof(FirmwareHeader), package_length);
      return ZX_ERR_NO_MEMORY;
    }

    char firmware_format[sizeof(header->data.format) + 1] = {};
    memcpy(firmware_format, header->data.format, sizeof(header->data.format));
    LOG(INFO, "firmware_format: %s", firmware_format);

    FirmwareCode code = {offset + sizeof(FirmwareHeader), firmware_length};
    firmware_code_[std::string(firmware_format)] = code;

    offset += package_length;
  }
  return ZX_OK;
}

namespace {

std::string FirmwareTypeToName(FirmwareBlob::FirmwareType type) {
  switch (type) {
    case FirmwareBlob::FirmwareType::kDec_Mpeg12: return "mpeg12";
    case FirmwareBlob::FirmwareType::kDec_Mpeg4_3: return "divx311";
    case FirmwareBlob::FirmwareType::kDec_Mpeg4_4: return "divx4x";
    case FirmwareBlob::FirmwareType::kDec_Mpeg4_5: return "xvid";
    case FirmwareBlob::FirmwareType::kDec_H263: return "h263";
    case FirmwareBlob::FirmwareType::kDec_Mjpeg: return "mjpeg";
    case FirmwareBlob::FirmwareType::kDec_Mjpeg_Multi: return "mjpeg_multi";
    case FirmwareBlob::FirmwareType::kDec_Real_v8: return "real_v8";
    case FirmwareBlob::FirmwareType::kDec_Real_v9: return "real_v9";
    case FirmwareBlob::FirmwareType::kDec_Vc1: return "vc1";
    case FirmwareBlob::FirmwareType::kDec_Avs: return "avs";
    case FirmwareBlob::FirmwareType::kDec_H264: return "h264";
    case FirmwareBlob::FirmwareType::kDec_H264_4k2k: return "h264_4k2k";
    case FirmwareBlob::FirmwareType::kDec_H264_4k2k_Single: return "h264_4k2k_single";
    case FirmwareBlob::FirmwareType::kDec_H264_Mvc: return "h264_mvc";
    case FirmwareBlob::FirmwareType::kDec_H264_Multi: return "h264_multi";
    case FirmwareBlob::FirmwareType::kDec_Hevc: return "hevc";
    case FirmwareBlob::FirmwareType::kDec_Hevc_Mmu: return "hevc_mmu";
    case FirmwareBlob::FirmwareType::kDec_Vp9: return "vp9";
    case FirmwareBlob::FirmwareType::kDec_Vp9_Mmu: return "vp9_mmu";
    case FirmwareBlob::FirmwareType::kEnc_H264: return "h264_enc";
    case FirmwareBlob::FirmwareType::kEnc_Jpeg: return "jpeg_enc";
    // value 22 kPackage is missing intentionally - 22 isn't a firmware
    case FirmwareBlob::FirmwareType::kDec_H264_Multi_Mmu: return "h264_multi_mmu";
    case FirmwareBlob::FirmwareType::kDec_Hevc_G12a: return "hevc_g12a";
    case FirmwareBlob::FirmwareType::kDec_Vp9_G12a: return "vp9_g12a";
    case FirmwareBlob::FirmwareType::kDec_Avs2: return "avs2";
    case FirmwareBlob::FirmwareType::kDec_Avs2_Mmu: return "avs2_mmu";
    case FirmwareBlob::FirmwareType::kDec_Avs_Gxm: return "avs_gxm";
    case FirmwareBlob::FirmwareType::kDec_Avs_NoCabac: return "avs_no_cabac";
    case FirmwareBlob::FirmwareType::kDec_H264_Multi_Gxm: return "h264_multi_gxm";
    case FirmwareBlob::FirmwareType::kDec_H264_Mvc_Gxm: return "h264_mvc_gxm";
    case FirmwareBlob::FirmwareType::kDec_Vc1_G12a: return "vc1_g12a";
    default:
      LOG(ERROR, "Unrecognized firmware type: %d\n", type);
      return "";
  }
}

}  // namespace

zx_status_t FirmwareBlob::GetFirmwareData(FirmwareType firmware_type, uint8_t** data_out,
                                          uint32_t* size_out) {
  std::string format_name = FirmwareTypeToName(firmware_type);
  if (format_name == "")
    return ZX_ERR_INVALID_ARGS;
  auto it = firmware_code_.find(format_name);
  if (it == firmware_code_.end()) {
    DECODE_ERROR("Couldn't find firmware type: %d\n", firmware_type);
    return ZX_ERR_INVALID_ARGS;
  }
  *data_out = reinterpret_cast<uint8_t*>(ptr_) + it->second.offset;
  *size_out = it->second.size;
  return ZX_OK;
}

void FirmwareBlob::GetWholeBlob(uint8_t** data_out, uint32_t* size_out) {
  // This must not be called if LoadFirmware() failed.
  ZX_DEBUG_ASSERT(ptr_);
  *data_out = reinterpret_cast<uint8_t*>(ptr_);
  *size_out = fw_size_;
}

void FirmwareBlob::LoadFakeFirmwareForTesting(FirmwareType firmware_type, uint8_t* data,
                                              uint32_t size) {
  std::string format_name = FirmwareTypeToName(firmware_type);
  assert(ptr_ == 0);

  ptr_ = reinterpret_cast<uintptr_t>(data);

  firmware_code_[format_name].size = size;
  firmware_code_[format_name].offset = 0;
}
