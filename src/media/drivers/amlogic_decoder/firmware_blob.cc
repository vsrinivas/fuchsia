// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "firmware_blob.h"

#include <lib/ddk/debug.h>
#include <lib/zx/vmar.h>
#include <zircon/assert.h>

#include <vector>

namespace amlogic_decoder {

FirmwareBlob::~FirmwareBlob() {
  if (vmo_)
    zx::vmar::root_self()->unmap(ptr_, fw_size_);
}

zx_status_t FirmwareBlob::LoadFirmware(zx_device_t* device) {
  zx_status_t status =
      load_firmware(device, "amlogic_video_ucode.bin", vmo_.reset_and_get_address(), &fw_size_);
  if (status != ZX_OK) {
    DECODE_ERROR("Couldn't load amlogic firmware");
    return status;
  }

  zx::vmar::root_self()->map(ZX_VM_PERM_READ, 0, vmo_, 0, fw_size_, &ptr_);
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
      DECODE_ERROR("PackageHeader doesn't fit in data");
      return ZX_ERR_NO_MEMORY;
    }

    auto header = reinterpret_cast<PackageEntryHeader*>(data + offset);

    offset += sizeof(PackageEntryHeader);
    uint32_t package_length = header->data.length;
    if (offset + package_length > fw_size_) {
      DECODE_ERROR("Package too long");
      return ZX_ERR_NO_MEMORY;
    }
    if (sizeof(FirmwareHeader) > package_length) {
      DECODE_ERROR("FirmwareHeader doesn't fit in data %d", package_length);
      return ZX_ERR_NO_MEMORY;
    }

    FirmwareHeader* firmware_data = reinterpret_cast<FirmwareHeader*>(data + offset);
    uint32_t firmware_length = firmware_data->data.data_size;
    if (static_cast<uint64_t>(firmware_length) + sizeof(FirmwareHeader) > package_length) {
      DECODE_ERROR("Firmware data doesn't fit in data %d %ld %d", firmware_length,
                   sizeof(FirmwareHeader), package_length);
      return ZX_ERR_NO_MEMORY;
    }

    char firmware_cpu[sizeof(header->data.cpu) + 1] = {};
    char firmware_format[sizeof(header->data.format) + 1] = {};
    memcpy(firmware_cpu, header->data.cpu, sizeof(header->data.cpu));
    memcpy(firmware_format, header->data.format, sizeof(header->data.format));

    constexpr bool kLogFirmwares = false;
    if (kLogFirmwares) {
      // To help diagnose firmware loading problems.
      char firmware_name[sizeof(header->data.name) + 1] = {};
      memcpy(firmware_name, header->data.name, sizeof(header->data.name));
      LOG(INFO, "firmware_format: %s firmware_cpu: %s firmware_name: %s",
          std::string(firmware_format).c_str(), std::string(firmware_cpu).c_str(),
          std::string(firmware_name).c_str());
    }

    FirmwareCode code = {offset + sizeof(FirmwareHeader), firmware_length};
    firmware_code_[{firmware_cpu, firmware_format}] = code;

    offset += package_length;
  }
  return ZX_OK;
}

namespace {

std::string FirmwareTypeToFormat(FirmwareBlob::FirmwareType type) {
  switch (type) {
    case FirmwareBlob::FirmwareType::kDec_Mpeg12:
      return "mpeg12";
    case FirmwareBlob::FirmwareType::kDec_Mpeg4_3:
      return "divx311";
    case FirmwareBlob::FirmwareType::kDec_Mpeg4_4:
      return "divx4x";
    case FirmwareBlob::FirmwareType::kDec_Mpeg4_5:
      return "xvid";
    case FirmwareBlob::FirmwareType::kDec_H263:
      return "h263";
    case FirmwareBlob::FirmwareType::kDec_Mjpeg:
      return "mjpeg";
    case FirmwareBlob::FirmwareType::kDec_Mjpeg_Multi:
      return "mjpeg_multi";
    case FirmwareBlob::FirmwareType::kDec_Real_v8:
      return "real_v8";
    case FirmwareBlob::FirmwareType::kDec_Real_v9:
      return "real_v9";
    case FirmwareBlob::FirmwareType::kDec_Vc1:
      return "vc1";
    case FirmwareBlob::FirmwareType::kDec_Avs:
      return "avs";
    case FirmwareBlob::FirmwareType::kDec_H264:
      return "h264";
    case FirmwareBlob::FirmwareType::kDec_H264_4k2k:
      return "h264_4k2k";
    case FirmwareBlob::FirmwareType::kDec_H264_4k2k_Single:
      return "h264_4k2k_single";
    case FirmwareBlob::FirmwareType::kDec_H264_Mvc:
      return "h264_mvc";
    case FirmwareBlob::FirmwareType::kDec_H264_Multi:
      return "h264_multi";
    case FirmwareBlob::FirmwareType::kDec_Hevc:
      return "hevc";
    case FirmwareBlob::FirmwareType::kDec_Hevc_Mmu:
      return "hevc_mmu";
    case FirmwareBlob::FirmwareType::kDec_Vp9:
      return "vp9";
    case FirmwareBlob::FirmwareType::kDec_Vp9_Mmu:
      return "vp9_mmu";
    case FirmwareBlob::FirmwareType::kEnc_H264:
      return "h264_enc";
    case FirmwareBlob::FirmwareType::kEnc_Jpeg:
      return "jpeg_enc";
    // value 22 kPackage is missing intentionally - 22 isn't a firmware
    case FirmwareBlob::FirmwareType::kDec_H264_Multi_Mmu:
      return "h264_multi_mmu";
    case FirmwareBlob::FirmwareType::kDec_Hevc_G12a:
      return "hevc_g12a";
    case FirmwareBlob::FirmwareType::kDec_Vp9_G12a:
      return "vp9_g12a";
    case FirmwareBlob::FirmwareType::kDec_Avs2:
      return "avs2";
    case FirmwareBlob::FirmwareType::kDec_Avs2_Mmu:
      return "avs2_mmu";
    case FirmwareBlob::FirmwareType::kDec_Avs_Gxm:
      return "avs_gxm";
    case FirmwareBlob::FirmwareType::kDec_Avs_NoCabac:
      return "avs_no_cabac";
    case FirmwareBlob::FirmwareType::kDec_H264_Multi_Gxm:
      return "h264_multi_gxm";
    case FirmwareBlob::FirmwareType::kDec_H264_Mvc_Gxm:
      return "h264_mvc_gxm";
    case FirmwareBlob::FirmwareType::kDec_Vc1_G12a:
      return "vc1_g12a";
    default:
      LOG(ERROR, "Unrecognized firmware type: %d", type);
      return "";
  }
}

std::vector<std::string> DeviceTypeToCpu(DeviceType device_type) {
  std::vector<std::string> cpu_names;
  switch (device_type) {
    case DeviceType::kGXM:
      cpu_names.push_back("gxm");
      break;
    case DeviceType::kG12B:
      cpu_names.push_back("g12b");
      // Sometimes G12b shares firmware with G12a and GXM. But always match G12b before G12a, then
      // GXM. Do not change the order!
      cpu_names.push_back("g12a");
      cpu_names.push_back("gxm");
      break;
    case DeviceType::kG12A:
      cpu_names.push_back("g12a");
      break;
    case DeviceType::kSM1:
      cpu_names.push_back("sm1");
      break;
    default:
      LOG(ERROR, "Unrecognized device type: %d", device_type);
  }
  return cpu_names;
}

}  // namespace

zx_status_t FirmwareBlob::GetFirmwareData(FirmwareType firmware_type, uint8_t** data_out,
                                          uint32_t* size_out) {
  auto cpu_names = DeviceTypeToCpu(device_type_);
  auto format_name = FirmwareTypeToFormat(firmware_type);
  if (cpu_names.empty() || format_name.empty())
    return ZX_ERR_INVALID_ARGS;
  for (auto& cpu_name : cpu_names) {
    auto it = firmware_code_.find({cpu_name, format_name});
    if (it != firmware_code_.end()) {
      *data_out = reinterpret_cast<uint8_t*>(ptr_) + it->second.offset;
      *size_out = it->second.size;
      LOG(INFO, "Got firmware with cpu_name %s and format %s for type %d and device type %d",
          &cpu_name[0], &format_name[0], firmware_type, device_type_);
      return ZX_OK;
    }
  }
  DECODE_ERROR("Couldn't find firmware for type: %d and device type: %d", firmware_type,
               device_type_);
  return ZX_ERR_INVALID_ARGS;
}

void FirmwareBlob::GetWholeBlob(uint8_t** data_out, uint32_t* size_out) {
  // This must not be called if LoadFirmware() failed.
  ZX_DEBUG_ASSERT(ptr_);
  *data_out = reinterpret_cast<uint8_t*>(ptr_);
  *size_out = fw_size_;
}

void FirmwareBlob::LoadFakeFirmwareForTesting(FirmwareType firmware_type, uint8_t* data,
                                              uint32_t size) {
  auto cpu_names = DeviceTypeToCpu(device_type_);
  std::string format_name = FirmwareTypeToFormat(firmware_type);
  assert(ptr_ == 0);

  ptr_ = reinterpret_cast<uintptr_t>(data);
  firmware_code_[{cpu_names[0], format_name}].size = size;
  firmware_code_[{cpu_names[0], format_name}].offset = 0;
}

}  // namespace amlogic_decoder
