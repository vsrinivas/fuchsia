// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "h264_multi_decoder.h"

#include <lib/media/codec_impl/codec_buffer.h>
#include <lib/trace/event.h>

#include <cmath>
#include <iterator>
#include <limits>

#include <fbl/algorithm.h>

#include "h264_utils.h"
#include "media/base/decoder_buffer.h"
#include "media/gpu/h264_decoder.h"
#include "media/video/h264_level_limits.h"
#include "parser.h"
#include "registers.h"
#include "util.h"
#include "watchdog.h"

namespace {
// See VLD_PADDING_SIZE.
constexpr uint32_t kPaddingSize = 1024;
const uint8_t kPadding[kPaddingSize] = {};

// ISO 14496 part 10
// VUI parameters: Table E-1 "Meaning of sample aspect ratio indicator"
static const int kTableSarWidth[] = {0,  1,  12, 10, 16,  40, 24, 20, 32,
                                     80, 18, 15, 64, 160, 4,  3,  2};
static const int kTableSarHeight[] = {0,  1,  11, 11, 11, 33, 11, 11, 11,
                                      33, 11, 11, 33, 99, 3,  2,  1};
static_assert(base::size(kTableSarWidth) == base::size(kTableSarHeight),
              "sar tables must have the same size");

enum class ChromaFormatIdc : uint32_t {
  kMonochrome = 0,
  // Presently only 4:2:0 chroma_format_idc is supported:
  k420 = 1,
  k422 = 2,
  k444 = 3,
};

static constexpr uint32_t kMacroblockDimension = 16;

}  // namespace

class AmlogicH264Picture : public media::H264Picture {
 public:
  explicit AmlogicH264Picture(std::shared_ptr<H264MultiDecoder::ReferenceFrame> pic)
      : internal_picture(pic) {}
  ~AmlogicH264Picture() override {
    auto pic = internal_picture.lock();
    if (pic)
      pic->in_internal_use = false;
  }

  std::weak_ptr<H264MultiDecoder::ReferenceFrame> internal_picture;
};
class MultiAccelerator : public media::H264Decoder::H264Accelerator {
 public:
  explicit MultiAccelerator(H264MultiDecoder* owner) : owner_(owner) {}

  scoped_refptr<media::H264Picture> CreateH264Picture() override {
    DLOG("Got MultiAccelerator::CreateH264Picture");
    auto pic = owner_->GetUnusedReferenceFrame();
    if (!pic)
      return nullptr;
    return std::make_shared<AmlogicH264Picture>(pic);
  }

  Status SubmitFrameMetadata(const media::H264SPS* sps, const media::H264PPS* pps,
                             const media::H264DPB& dpb,
                             const media::H264Picture::Vector& ref_pic_listp0,
                             const media::H264Picture::Vector& ref_pic_listb0,
                             const media::H264Picture::Vector& ref_pic_listb1,
                             scoped_refptr<media::H264Picture> pic) override {
    DLOG("Got MultiAccelerator::SubmitFrameMetadata");
    ZX_DEBUG_ASSERT(owner_->currently_decoding());
    auto ref_pic = static_cast<AmlogicH264Picture*>(pic.get())->internal_picture.lock();
    if (!ref_pic)
      return Status::kFail;
    // struct copy
    current_sps_ = *sps;
    owner_->SubmitFrameMetadata(ref_pic.get(), sps, pps, dpb);
    return Status::kOk;
  }

  Status SubmitSlice(const media::H264PPS* pps, const media::H264SliceHeader* slice_hdr,
                     const media::H264Picture::Vector& ref_pic_list0,
                     const media::H264Picture::Vector& ref_pic_list1,
                     scoped_refptr<media::H264Picture> pic, const uint8_t* data, size_t size,
                     const std::vector<media::SubsampleEntry>& subsamples) override {
    ZX_DEBUG_ASSERT(owner_->currently_decoding());
    DLOG("Got MultiAccelerator::SubmitSlice");
    H264MultiDecoder::SliceData slice_data;
    // struct copy
    slice_data.sps = current_sps_;
    // struct copy
    slice_data.pps = *pps;
    // struct copy
    slice_data.header = *slice_hdr;
    slice_data.pic = pic;
    slice_data.ref_pic_list0 = ref_pic_list0;
    slice_data.ref_pic_list1 = ref_pic_list1;
    owner_->SubmitSliceData(std::move(slice_data));
    return Status::kOk;
  }

  Status SubmitDecode(scoped_refptr<media::H264Picture> pic) override {
    ZX_DEBUG_ASSERT(owner_->currently_decoding());
    auto ref_pic = static_cast<AmlogicH264Picture*>(pic.get())->internal_picture.lock();
    if (!ref_pic)
      return Status::kFail;
    DLOG("Got MultiAccelerator::SubmitDecode picture %d", ref_pic->index);
    return Status::kOk;
  }

  bool OutputPicture(scoped_refptr<media::H264Picture> pic) override {
    auto ref_pic = static_cast<AmlogicH264Picture*>(pic.get())->internal_picture.lock();
    if (!ref_pic)
      return false;
    DLOG("Got MultiAccelerator::OutputPicture picture %d", ref_pic->index);
    owner_->OutputFrame(ref_pic.get(), pic->bitstream_id());
    return true;
  }

  void Reset() override {}

  Status SetStream(base::span<const uint8_t> stream,
                   const media::DecryptConfig* decrypt_config) override {
    ZX_DEBUG_ASSERT_MSG(false, "unreachable");
    return Status::kOk;
  }

 private:
  H264MultiDecoder* owner_;
  media::H264SPS current_sps_;
};

using InitFlagReg = AvScratch2;
using HeadPaddingReg = AvScratch3;
using H264DecodeModeReg = AvScratch4;
using H264DecodeSeqInfo = AvScratch5;
using NalSearchCtl = AvScratch9;
using ErrorStatusReg = AvScratch9;
using H264AuxAddr = AvScratchC;
using H264DecodeSizeReg = AvScratchE;
using H264AuxDataSize = AvScratchH;
using FrameCounterReg = AvScratchI;
using DpbStatusReg = AvScratchJ;
using LmemDumpAddr = AvScratchL;
using DebugReg1 = AvScratchM;
using DebugReg2 = AvScratchN;

using H264DecodeInfo = M4ControlReg;

// AvScratch1
class StreamInfo : public TypedRegisterBase<DosRegisterIo, StreamInfo, uint32_t> {
 public:
  DEF_FIELD(7, 0, width_in_mbs);
  DEF_FIELD(23, 8, total_mbs);

  // The upper_signficant bits are provided back to HW in some cases, but we don't (yet) know if
  // these bits really matter for that purpose.
  //
  // The amlogic code considers upper_signficant bits when determining whether to allocate buffers,
  // but this driver doesn't.
  DEF_FIELD(30, 24, upper_significant);

  // This bit is not provided back to HW, and not considered by amlogic code or this driver for
  // determining whether to allocate buffers.
  DEF_FIELD(31, 31, insignificant);

  static auto Get() { return AddrType(0x09c1 * 4); }
};

// AvScratch2
class SequenceInfo : public TypedRegisterBase<DosRegisterIo, SequenceInfo, uint32_t> {
 public:
  DEF_BIT(0, aspect_ratio_info_present_flag);
  DEF_BIT(1, timing_info_present_flag);
  DEF_BIT(4, pic_struct_present_flag);

  // relatively lower-confidence vs. other bits - not confirmed
  DEF_BIT(6, fixed_frame_rate_flag);

  // This apparently is reliably 3 for 4:2:2 separate color plane, or not 3.
  // For non-IDC 4:2:0 frames, this can be 0 instead of the 1 it seems like it should be.
  DEF_FIELD(14, 13, chroma_format_idc);
  DEF_BIT(15, frame_mbs_only_flag);
  DEF_FIELD(23, 16, aspect_ratio_idc);

  static auto Get() { return AddrType(0x09c2 * 4); }
};

// AvScratch6
class CropInfo : public TypedRegisterBase<DosRegisterIo, CropInfo, uint32_t> {
 public:
  // All quantities are the number of pixels to be cropped from each side.
  DEF_FIELD(7, 0, bottom);
  DEF_FIELD(15, 8, top);  // Ignored and unconfirmed
  DEF_FIELD(23, 16, right);
  DEF_FIELD(31, 24, left);  // Ignored and unconfirmed

  static auto Get() { return AddrType(0x09c6 * 4); }
};

// AvScratchB
class StreamInfo2 : public TypedRegisterBase<DosRegisterIo, StreamInfo2, uint32_t> {
 public:
  DEF_FIELD(7, 0, level_idc);
  DEF_FIELD(15, 8, max_reference_size);

  static auto Get() { return AddrType(0x09cb * 4); }
};

// AvScratchF
class CodecSettings : public TypedRegisterBase<DosRegisterIo, CodecSettings, uint32_t> {
 public:
  DEF_BIT(1, trickmode_i);
  DEF_BIT(2, zeroed0);
  DEF_BIT(3, drop_b_frames);
  DEF_BIT(4, error_recovery_mode);
  DEF_BIT(5, zeroed1);
  DEF_BIT(6, ip_frames_only);
  DEF_BIT(7, disable_fast_poc);

  static auto Get() { return AddrType(0x09cf * 4); }
};

enum DecodeMode {
  // Mode where multiple streams can be decoded, and input doesn't have to be
  // broken into frame-sized chunks.
  kDecodeModeMultiStreamBased = 0x2
};

// Actions written by CPU into DpbStatusReg to tell the firmware what to do.
enum H264Action {
  // Start searching for the head of a frame to decode.
  kH264ActionSearchHead = 0xf0,

  // Done responding to a config request.
  kH264ActionConfigDone = 0xf2,

  // Decode a slice (not the first one) in a picture.
  kH264ActionDecodeSlice = 0xf1,

  // Decode the first slice in a new picture.
  kH264ActionDecodeNewpic = 0xf3,
};

// Actions written by the firmware into DpbStatusReg before an interrupt to tell
// the CPU what to do.
enum H264Status {
  // Configure the DPB.
  kH264ConfigRequest = 0x11,

  // Out of input data, so get more.
  kH264DataRequest = 0x12,

  // The firmware detected the hardware timed out while attempting to decode.
  kH264DecodeTimeout = 0x21,

  // kH264ActionSearchHead wasn't able to find a frame to decode.
  kH264SearchBufempty = 0x22,

  // Initialize the current set of reference frames and output buffer to be
  // decoded into.
  kH264SliceHeadDone = 0x1,

  // Store the current frame into the DPB, or output it.
  kH264PicDataDone = 0x2,
};

H264MultiDecoder::H264MultiDecoder(Owner* owner, Client* client, FrameDataProvider* provider,
                                   bool is_secure)
    : VideoDecoder(owner, client, is_secure), frame_data_provider_(provider) {
  media_decoder_ = std::make_unique<media::H264Decoder>(std::make_unique<MultiAccelerator>(this),
                                                        media::H264PROFILE_HIGH);
  use_parser_ = true;
}

H264MultiDecoder::~H264MultiDecoder() {
  if (owner_->IsDecoderCurrent(this)) {
    owner_->watchdog()->Cancel();
    owner_->core()->StopDecoding();
    owner_->core()->WaitForIdle();
  }
  BarrierBeforeRelease();
}

zx_status_t H264MultiDecoder::Initialize() {
  zx_status_t status = InitializeBuffers();
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to initialize buffers");
    return status;
  }

  return InitializeHardware();
}

zx_status_t H264MultiDecoder::LoadSecondaryFirmware(const uint8_t* data, uint32_t firmware_size) {
  TRACE_DURATION("media", "H264MultiDecoder::LoadSecondaryFirmware");
  ZX_DEBUG_ASSERT(!secondary_firmware_);
  // For some reason, some portions of the firmware aren't loaded into the
  // hardware directly, but are kept in main memory.
  constexpr uint32_t kSecondaryFirmwareSize = 4 * 1024;
  // Some sections of the input firmware are copied into multiple places in the output buffer, and 1
  // part of the output buffer seems to be unused.
  constexpr uint32_t kFirmwareSectionCount = 9;
  constexpr uint32_t kSecondaryFirmwareBufferSize = kSecondaryFirmwareSize * kFirmwareSectionCount;
  constexpr uint32_t kBufferAlignShift = 16;
  auto result = InternalBuffer::CreateAligned(
      "H264MultiSecondaryFirmware", &owner_->SysmemAllocatorSyncPtr(), owner_->bti(),
      kSecondaryFirmwareBufferSize, 1 << kBufferAlignShift, /*is_secure*/ false,
      /*is_writable=*/true, /*is_mapping_needed*/ true);
  if (!result.is_ok()) {
    DECODE_ERROR("Failed to make second firmware buffer: %d", result.error());
    return result.error();
  }

  secondary_firmware_.emplace(result.take_value());

  auto addr = static_cast<uint8_t*>(secondary_firmware_->virt_base());
  // The secondary firmware is in a different order in the file than the main
  // firmware expects it to have.
  memcpy(addr + 0, data + 0x4000, kSecondaryFirmwareSize);                // header
  memcpy(addr + 0x1000, data + 0x2000, kSecondaryFirmwareSize);           // data
  memcpy(addr + 0x2000, data + 0x6000, kSecondaryFirmwareSize);           // mmc
  memcpy(addr + 0x3000, data + 0x3000, kSecondaryFirmwareSize);           // list
  memcpy(addr + 0x4000, data + 0x5000, kSecondaryFirmwareSize);           // slice
  memcpy(addr + 0x5000, data, 0x2000);                                    // main
  memcpy(addr + 0x5000 + 0x2000, data + 0x2000, kSecondaryFirmwareSize);  // data copy 2
  memcpy(addr + 0x5000 + 0x3000, data + 0x5000, kSecondaryFirmwareSize);  // slice copy 2
  ZX_DEBUG_ASSERT(0x5000 + 0x3000 + kSecondaryFirmwareSize == kSecondaryFirmwareBufferSize);
  secondary_firmware_->CacheFlush(0, kSecondaryFirmwareBufferSize);
  BarrierAfterFlush();
  return ZX_OK;
}

constexpr uint32_t kAuxBufPrefixSize = 16 * 1024;
constexpr uint32_t kAuxBufSuffixSize = 0;

zx_status_t H264MultiDecoder::InitializeBuffers() {
  // Don't use the TEE to load the firmware, since the version we're using on astro and sherlock
  // doesn't support H264_Multi_Gxm.
  FirmwareBlob::FirmwareType firmware_type = FirmwareBlob::FirmwareType::kDec_H264_Multi_Gxm;
  uint8_t* data;
  uint32_t firmware_size;
  zx_status_t status =
      owner_->firmware_blob()->GetFirmwareData(firmware_type, &data, &firmware_size);
  if (status != ZX_OK)
    return status;
  static constexpr uint32_t kFirmwareSize = 4 * 4096;
  const uint32_t kBufferAlignShift = 16;
  if (firmware_size < kFirmwareSize) {
    DECODE_ERROR("Firmware too small");
    return ZX_ERR_INTERNAL;
  }

  {
    auto create_result = InternalBuffer::CreateAligned(
        "H264MultiFirmware", &owner_->SysmemAllocatorSyncPtr(), owner_->bti(), kFirmwareSize,
        1 << kBufferAlignShift, /*is_secure=*/false, /*is_writable=*/true,
        /*is_mapping_needed=*/true);
    if (!create_result.is_ok()) {
      DECODE_ERROR("Failed to make firmware buffer - %d", create_result.error());
      return {};
    }
    firmware_ = create_result.take_value();
    memcpy(firmware_->virt_base(), data, kFirmwareSize);
    firmware_->CacheFlush(0, kFirmwareSize);
    BarrierAfterFlush();
  }
  status = LoadSecondaryFirmware(data, firmware_size);
  if (status != ZX_OK) {
    return status;
  }

  constexpr uint32_t kBufferAlignment = 1 << 16;
  constexpr uint32_t kCodecDataSize = 0x200000;
  auto codec_data_create_result =
      InternalBuffer::CreateAligned("H264MultiCodecData", &owner_->SysmemAllocatorSyncPtr(),
                                    owner_->bti(), kCodecDataSize, kBufferAlignment, is_secure(),
                                    /*is_writable=*/true, /*is_mapping_needed*/ false);
  if (!codec_data_create_result.is_ok()) {
    LOG(ERROR, "Failed to make codec data buffer - status: %d", codec_data_create_result.error());
    return codec_data_create_result.error();
  }
  codec_data_.emplace(codec_data_create_result.take_value());

  // Aux buf seems to be used for reading SEI data.
  constexpr uint32_t kAuxBufSize = kAuxBufPrefixSize + kAuxBufSuffixSize;
  auto aux_buf_create_result =
      InternalBuffer::CreateAligned("H264AuxBuf", &owner_->SysmemAllocatorSyncPtr(), owner_->bti(),
                                    kAuxBufSize, kBufferAlignment, /*is_secure=*/false,
                                    /*is_writable=*/true, /*is_mapping_needed*/ false);
  if (!aux_buf_create_result.is_ok()) {
    LOG(ERROR, "Failed to make aux buffer - status: %d", aux_buf_create_result.error());
    return aux_buf_create_result.error();
  }
  aux_buf_.emplace(aux_buf_create_result.take_value());

  // Lmem is used to dump the AMRISC's local memory, which is needed for updating the DPB.
  constexpr uint32_t kLmemBufSize = 4096;
  auto lmem_create_result =
      InternalBuffer::CreateAligned("H264Lmem", &owner_->SysmemAllocatorSyncPtr(), owner_->bti(),
                                    kLmemBufSize, kBufferAlignment, /*is_secure=*/false,
                                    /*is_writable=*/true, /*is_mapping_needed*/ true);
  if (!lmem_create_result.is_ok()) {
    LOG(ERROR, "Failed to make lmem buffer - status: %d", lmem_create_result.error());
    return lmem_create_result.error();
  }
  lmem_.emplace(lmem_create_result.take_value());

  return ZX_OK;
}

void H264MultiDecoder::ResetHardware() {
  TRACE_DURATION("media", "H264MultiDecoder::ResetHardware");
  DosSwReset0::Get().FromValue(0).set_vdec_mc(1).set_vdec_iqidct(1).set_vdec_vld_part(1).WriteTo(
      owner_->dosbus());
  DosSwReset0::Get().FromValue(0).WriteTo(owner_->dosbus());

  // Reads are used for delaying running later code.
  for (uint32_t i = 0; i < 3; i++) {
    DosSwReset0::Get().ReadFrom(owner_->dosbus());
  }

  DosSwReset0::Get().FromValue(0).set_vdec_mc(1).set_vdec_iqidct(1).set_vdec_vld_part(1).WriteTo(
      owner_->dosbus());
  DosSwReset0::Get().FromValue(0).WriteTo(owner_->dosbus());

  DosSwReset0::Get().FromValue(0).set_vdec_pic_dc(1).set_vdec_dblk(1).WriteTo(owner_->dosbus());
  DosSwReset0::Get().FromValue(0).WriteTo(owner_->dosbus());

  // Reads are used for delaying running later code.
  for (uint32_t i = 0; i < 3; i++) {
    DosSwReset0::Get().ReadFrom(owner_->dosbus());
  }

  auto temp = PowerCtlVld::Get().ReadFrom(owner_->dosbus());
  temp.set_reg_value(temp.reg_value() | (1 << 9) | (1 << 6));
  temp.WriteTo(owner_->dosbus());
}

zx_status_t H264MultiDecoder::InitializeHardware() {
  TRACE_DURATION("media", "H264MultiDecoder::InitializeHardware");
  ZX_DEBUG_ASSERT(state_ == DecoderState::kSwappedOut);
  ZX_DEBUG_ASSERT(owner_->IsDecoderCurrent(this));
  zx_status_t status =
      owner_->SetProtected(VideoDecoder::Owner::ProtectableHardwareUnit::kVdec, is_secure());
  if (status != ZX_OK)
    return status;

  status = owner_->core()->LoadFirmware(*firmware_);
  if (status != ZX_OK)
    return status;

  ResetHardware();
  AvScratchG::Get()
      .FromValue(truncate_to_32(secondary_firmware_->phys_base()))
      .WriteTo(owner_->dosbus());

  PscaleCtrl::Get().FromValue(0).WriteTo(owner_->dosbus());
  VdecAssistMbox1ClrReg::Get().FromValue(1).WriteTo(owner_->dosbus());
  VdecAssistMbox1Mask::Get().FromValue(1).WriteTo(owner_->dosbus());
  {
    auto temp = MdecPicDcCtrl::Get().ReadFrom(owner_->dosbus()).set_nv12_output(true);
    temp.set_reg_value(temp.reg_value() | (0xbf << 24));
    temp.WriteTo(owner_->dosbus());
    temp.set_reg_value(temp.reg_value() & ~(0xbf << 24));
    temp.WriteTo(owner_->dosbus());
  }
  MdecPicDcMuxCtrl::Get().ReadFrom(owner_->dosbus()).set_bit31(0).WriteTo(owner_->dosbus());
  MdecExtIfCfg0::Get().FromValue(0).WriteTo(owner_->dosbus());
  MdecPicDcThresh::Get().FromValue(0x404038aa).WriteTo(owner_->dosbus());

  // Signal that the DPB hasn't been initialized yet.
  if (video_frames_.size() > 0) {
    for (auto& frame : video_frames_) {
      AncNCanvasAddr::Get(frame->index)
          .FromValue((frame->uv_canvas->index() << 16) | (frame->uv_canvas->index() << 8) |
                     (frame->y_canvas->index()))
          .WriteTo(owner_->dosbus());
    }
    AvScratch7::Get()
        .FromValue(static_cast<uint32_t>((next_max_reference_size_ << 24) |
                                         (video_frames_.size() << 16) |
                                         (video_frames_.size() << 8)))
        .WriteTo(owner_->dosbus());
  } else {
    AvScratch0::Get().FromValue(0).WriteTo(owner_->dosbus());
    AvScratch9::Get().FromValue(0).WriteTo(owner_->dosbus());
  }
  DpbStatusReg::Get().FromValue(0).WriteTo(owner_->dosbus());

  FrameCounterReg::Get().FromValue(0).WriteTo(owner_->dosbus());

  constexpr uint32_t kBufferStartAddressOffset = 0x1000000;
  constexpr uint32_t kDcacReadMargin = 64 * 1024;
  uint32_t buffer_offset =
      truncate_to_32(codec_data_->phys_base()) - kBufferStartAddressOffset + kDcacReadMargin;
  AvScratch8::Get().FromValue(buffer_offset).WriteTo(owner_->dosbus());

  CodecSettings::Get()
      .ReadFrom(owner_->dosbus())
      .set_drop_b_frames(0)
      .set_zeroed0(0)
      .set_error_recovery_mode(1)
      .set_zeroed1(0)
      .set_ip_frames_only(0)
      .WriteTo(owner_->dosbus());

  LmemDumpAddr::Get().FromValue(truncate_to_32(lmem_->phys_base())).WriteTo(owner_->dosbus());
  DebugReg1::Get().FromValue(0).WriteTo(owner_->dosbus());
  DebugReg2::Get().FromValue(0).WriteTo(owner_->dosbus());
  H264DecodeInfo::Get().FromValue(1 << 13).WriteTo(owner_->dosbus());
  // TODO(fxb/13483): Use real values.
  constexpr uint32_t kBytesToDecode = 100000;
  H264DecodeSizeReg::Get().FromValue(kBytesToDecode).WriteTo(owner_->dosbus());
  ViffBitCnt::Get().FromValue(kBytesToDecode * 8).WriteTo(owner_->dosbus());

  H264AuxAddr::Get().FromValue(truncate_to_32(aux_buf_->phys_base())).WriteTo(owner_->dosbus());
  H264AuxDataSize::Get()
      .FromValue(((kAuxBufPrefixSize / 16) << 16) | (kAuxBufSuffixSize / 16))
      .WriteTo(owner_->dosbus());
  H264DecodeModeReg::Get().FromValue(kDecodeModeMultiStreamBased).WriteTo(owner_->dosbus());
  H264DecodeSeqInfo::Get().FromValue(seq_info2_).WriteTo(owner_->dosbus());
  HeadPaddingReg::Get().FromValue(0).WriteTo(owner_->dosbus());
  InitFlagReg::Get().FromValue(have_initialized_).WriteTo(owner_->dosbus());
  have_initialized_ = true;

  // TODO(fxb/13483): Set to 1 when SEI is supported.
  NalSearchCtl::Get().FromValue(0).WriteTo(owner_->dosbus());
  state_ = DecoderState::kWaitingForInputOrOutput;
  return ZX_OK;
}

void H264MultiDecoder::StartFrameDecode() {
  ZX_DEBUG_ASSERT(state_ == DecoderState::kWaitingForInputOrOutput);
  currently_decoding_ = true;

  // For now, just use the decode size from InitializeHardware.
  if (state_ == DecoderState::kWaitingForInputOrOutput) {
    // TODO(fxb/13483): Use real value.
    constexpr uint32_t kBytesToDecode = 100000;
    ViffBitCnt::Get().FromValue(kBytesToDecode * 8).WriteTo(owner_->dosbus());
    owner_->core()->StartDecoding();
  }
  DpbStatusReg::Get().FromValue(kH264ActionSearchHead).WriteTo(owner_->dosbus());
  state_ = DecoderState::kRunning;
  owner_->watchdog()->Start();
}

void H264MultiDecoder::ConfigureDpb() {
  ZX_DEBUG_ASSERT(currently_decoding_);
  owner_->watchdog()->Cancel();

  // The HW is told to continue decoding by writing DPB sizes to AvScratch0.  This can happen
  // immediately if the BufferCollection is already suitable, or after new sysmem allocation if
  // BufferCollection isn't suitable.

  // StreamInfo (aka AvScratch1)
  const auto seq_info2_value = StreamInfo::Get().ReadFrom(owner_->dosbus()).reg_value();
  auto seq_info2_tmp = StreamInfo::Get().FromValue(seq_info2_value);
  seq_info2_tmp.set_insignificant(0);
  // For local use in this method.
  const auto stream_info = StreamInfo::Get().FromValue(seq_info2_tmp.reg_value());
  // Stash for potentially restoring state in InitializeHardware().
  seq_info2_ = stream_info.reg_value();

  // SequenceInfo (aka AvScratch2)
  const auto sequence_info = SequenceInfo::Get().ReadFrom(owner_->dosbus());

  // CropInfo (aka AvScratch6)
  const auto crop_info = CropInfo::Get().ReadFrom(owner_->dosbus());

  // StreamInfo2 (aka AvScratchB)
  const auto stream_info2 = StreamInfo2::Get().ReadFrom(owner_->dosbus());

  if (!sequence_info.frame_mbs_only_flag()) {
    LOG(ERROR, "!sequence_info.frame_mbs_only_flag() - not supported");
    OnFatalError();
    return;
  }

  uint32_t mb_width = stream_info.width_in_mbs();
  // The maximum supported image width is 4096 bytes. The value of width_in_mbs should be 256 in
  // that case, but it wraps around since the field is only 8 bits. We need to correct for that
  // special case.
  if (!mb_width && stream_info.total_mbs())
    mb_width = 256;
  if (!mb_width) {
    DECODE_ERROR("0 mb_width");
    OnFatalError();
    return;
  }
  uint32_t mb_height = stream_info.total_mbs() / mb_width;

  uint32_t coded_width = mb_width * 16;
  uint32_t coded_height = mb_height * 16;
  constexpr uint32_t kMaxDimension = 4096;  // for both width and height.
  if (coded_width > kMaxDimension || coded_height > kMaxDimension) {
    LOG(ERROR, "Unsupported dimensions %dx%d", coded_width, coded_height);
    OnFatalError();
    return;
  }

  uint32_t stride = fbl::round_up(coded_width, 32u);
  if (coded_width <= crop_info.right()) {
    LOG(ERROR, "coded_width <= crop_info.right()");
    OnFatalError();
    return;
  }
  uint32_t display_width = coded_width - crop_info.right();
  if (coded_height <= crop_info.bottom()) {
    LOG(ERROR, "coded_height <= crop_info.bottom()");
    OnFatalError();
    return;
  }
  uint32_t display_height = coded_height - crop_info.bottom();

  // Compute max_dpb_size.  For a conformant stream, max_num_ref_frames is in the range
  // 0..max_dpb_frames, but take the max below anyway.  This is mostly adapted from H264Decoder's
  // DPB sizing code (but we need to know the DPB size before the fake SPS is with H264Decoder).
  uint32_t max_num_ref_frames = stream_info2.max_reference_size();
  uint32_t level = stream_info2.level_idc();
  if (level != 0) {
    hw_level_idc_ = level;
  } else {
    level = hw_level_idc_;
  }
  if (level == 0) {
    LOG(ERROR, "level == 0");
    OnFatalError();
    return;
  }
  if (level > std::numeric_limits<uint8_t>::max()) {
    LOG(ERROR, "level > std::numeric_limits<uint8_t>()::max()");
    OnFatalError();
    return;
  }
  uint32_t max_dpb_mbs = media::H264LevelToMaxDpbMbs(static_cast<uint8_t>(level));
  if (!max_dpb_mbs) {
    LOG(ERROR, "!max_dpb_mbs");
    OnFatalError();
    return;
  }
  // MaxDpbFrames from level limits per spec.
  uint32_t max_dpb_frames = std::min(max_dpb_mbs / (mb_width * mb_height),
                                     static_cast<uint32_t>(media::H264DPB::kDPBMaxSize));
  // Set DPB size to at least the level limit, or what the stream requires.
  uint32_t max_dpb_size = std::max(max_dpb_frames, max_num_ref_frames);

  uint32_t min_frame_count =
      std::min(max_dpb_size, static_cast<uint32_t>(media::H264DPB::kDPBMaxSize)) + 1;
  static constexpr uint32_t max_frame_count = 24;

  // Now we determine if new buffers are needed, and whether we need to re-config the decoder's
  // notion of the buffers.
  bool new_buffers_needed = false;
  bool config_update_needed = false;
  if (video_frames_.empty()) {
    new_buffers_needed = true;
    config_update_needed = true;
  }
  if (!new_buffers_needed && !client_->IsCurrentOutputBufferCollectionUsable(
                                 min_frame_count, max_frame_count, coded_width, coded_height,
                                 stride, display_width, display_height)) {
    new_buffers_needed = true;
  }
  if (new_buffers_needed) {
    config_update_needed = true;
  }
  if (!config_update_needed) {
    if (hw_coded_width_ != coded_width || hw_coded_height_ != coded_height ||
        hw_stride_ != stride || hw_display_width_ != display_width ||
        hw_display_height_ != display_height) {
      config_update_needed = true;
    }
  }
  ZX_DEBUG_ASSERT(!new_buffers_needed || config_update_needed);
  // For the moment, force new_buffers_needed if config_update_needed.
  //
  // TODO(dustingreen): Don't do this, and make sure we leave still-used downstream frames intact
  // until they're returned despite switching the frames to new image size within the existing
  // buffers (in preparation for emitting them again later at the new size).
  if (config_update_needed) {
    new_buffers_needed = true;
  }

  if (!new_buffers_needed && !config_update_needed) {
    // Tell HW to continue immediately.
    AvScratch0::Get()
        .FromValue(static_cast<uint32_t>((next_max_reference_size_ << 24) |
                                         (video_frames_.size() << 16) |
                                         (video_frames_.size() << 8)))
        .WriteTo(owner_->dosbus());
    owner_->watchdog()->Start();
    return;
  }

  if (new_buffers_needed) {
    // This also excludes separate_colour_plane_flag true.
    if (sequence_info.chroma_format_idc() != static_cast<uint32_t>(ChromaFormatIdc::k420) &&
        sequence_info.chroma_format_idc() != static_cast<uint32_t>(ChromaFormatIdc::kMonochrome)) {
      LOG(ERROR,
          "sequence_info.chroma_format_idc() not in {k420, kMonochrome} - "
          "sequence_info.chroma_format_idc(): %u",
          sequence_info.chroma_format_idc());
      OnFatalError();
      return;
    }

    state_ = DecoderState::kWaitingForConfigChange;
    // Don't tell core to StopDecoding() - currently_decoding_ remains true.
    ZX_DEBUG_ASSERT(currently_decoding_);
    if (!media_decoder_->Flush()) {
      LOG(ERROR, "!media_decoder_->Flush()");
      OnFatalError();
      return;
    }
    OutputReadyFrames();
    ZX_DEBUG_ASSERT(frames_to_output_.empty());
    video_frames_.clear();

    // TODO(fxb/13483): Reset initial I frame tracking if FW doesn't do that itself.

    // This is doing the same thing as the amlogic code, but it's unlikely to matter.  This has
    // basically nothing to do with the DPB size, and is just round-tripping a number back to the HW
    // like the amlogic code does.  The actual DPB size is separate (and also conveyed to the HW).
    // Since all the DPB management is in SW, it's unlikely that the FW or HW really cares about
    // this value, but just in case the HW would get annoyed, plumb this value.
    static constexpr uint32_t kHwMaxReferenceSizeAdjustment = 4;
    next_max_reference_size_ = stream_info2.max_reference_size() + kHwMaxReferenceSizeAdjustment;

    zx::bti bti;
    zx_status_t status = owner_->bti()->duplicate(ZX_RIGHT_SAME_RIGHTS, &bti);
    if (status != ZX_OK) {
      DECODE_ERROR("bti duplicate failed, status: %d\n", status);
      return;
    }

    pending_display_width_ = display_width;
    pending_display_height_ = display_height;
    // We handle SAR on the fly in this decoder since we don't get SAR until the slice header shows
    // up.  Or rather, that's when amlogic code gets SAR from the FW, so stick with that to avoid
    // reading at a different time than is known to work.
    static constexpr bool kHasSar = false;
    static constexpr uint32_t kSarWidth = 1;
    static constexpr uint32_t kSarHeight = 1;
    client_->InitializeFrames(std::move(bti), min_frame_count, max_frame_count, coded_width,
                              coded_height, stride, display_width, display_height, kHasSar,
                              kSarWidth, kSarHeight);
    waiting_for_surfaces_ = true;
    owner_->TryToReschedule();
    return;
  }

  if (config_update_needed) {
    // To be implemented and made reachable later maybe - higher priority would be keeping the same
    // buffers on seeking a stream though.  Presently the HW frame config update is happening in
    // InitializedFrames() directly, but we could factor that out and share it with this location.
    // This path would require not clearing video_frames_.
    ZX_PANIC("currently unreachable");
  }
}

bool H264MultiDecoder::InitializeRefPics(
    const std::vector<std::shared_ptr<media::H264Picture>>& ref_pic_list, uint32_t reg_offset) {
  uint32_t ref_list[8] = {};
  ZX_DEBUG_ASSERT(ref_pic_list.size() <= sizeof(ref_list));
  for (uint32_t i = 0; i < ref_pic_list.size(); i++) {
    DLOG("Getting pic list (for reg_offset %d) %d of %lu\n", reg_offset, i, ref_pic_list.size());
    auto* amlogic_picture = static_cast<AmlogicH264Picture*>(ref_pic_list[i].get());
    DLOG("amlogic_picture: %p", amlogic_picture);
    // amlogic_picture may be null if the decoder was recently flushed. In that case we don't have
    // information about what the reference frame was, so don't try to update it.
    if (!amlogic_picture)
      continue;
    auto internal_picture = amlogic_picture->internal_picture.lock();
    if (!internal_picture) {
      DECODE_ERROR("InitializeRefPics reg_offset %d missing internal picture %d", reg_offset, i);
      frame_data_provider_->AsyncResetStreamAfterCurrentFrame();
      return false;
    }

    // Offset into AncNCanvasAddr registers.
    uint32_t canvas_index = internal_picture->index;
    constexpr uint32_t kFrameFlag = 0x3;
    constexpr uint32_t kFieldTypeBitOffset = 5;
    uint32_t cfg = canvas_index | (kFrameFlag << kFieldTypeBitOffset);
    // Every dword stores 4 reference pics, lowest index in the highest bits.
    uint32_t offset_into_dword = 8 * (3 - (i % 4));
    ref_list[i / 4] |= (cfg << offset_into_dword);
  }

  H264BufferInfoIndex::Get().FromValue(reg_offset).WriteTo(owner_->dosbus());
  for (uint32_t reg_value : ref_list) {
    H264BufferInfoData::Get().FromValue(reg_value).WriteTo(owner_->dosbus());
  }
  return true;
}

void H264MultiDecoder::HandleSliceHeadDone() {
  ZX_DEBUG_ASSERT(owner_->IsDecoderCurrent(this));
  ZX_DEBUG_ASSERT(state_ == DecoderState::kRunning);
  owner_->watchdog()->Cancel();
  // Setup reference frames and output buffers before decoding.
  params_.ReadFromLmem(&*lmem_);
  DLOG("NAL unit type: %d\n", params_.data[HardwareRenderParams::kNalUnitType]);
  DLOG("NAL ref_idc: %d\n", params_.data[HardwareRenderParams::kNalRefIdc]);
  DLOG("NAL slice_type: %d\n", params_.data[HardwareRenderParams::kSliceType]);
  DLOG("pic order cnt type: %d\n", params_.data[HardwareRenderParams::kPicOrderCntType]);
  DLOG("log2_max_frame_num: %d\n", params_.data[HardwareRenderParams::kLog2MaxFrameNum]);
  DLOG("log2_max_pic_order_cnt: %d\n", params_.data[HardwareRenderParams::kLog2MaxPicOrderCntLsb]);
  DLOG("entropy coding mode flag: %d\n",
       params_.data[HardwareRenderParams::kEntropyCodingModeFlag]);
  DLOG("profile idc mmc0: %d\n", (params_.data[HardwareRenderParams::kProfileIdcMmco] >> 8) & 0xff);
  DLOG("Offset delimiter %d", params_.Read32(HardwareRenderParams::kOffsetDelimiterLo));
  DLOG("Mode 8x8 flags: 0x%x\n", params_.data[HardwareRenderParams::kMode8x8Flags]);

  // Don't need StreamInfo here - saved anything needed from there in ConfigureDpb().
  //
  // SequenceInfo (aka AvScratch2, aka "seq_info")
  const auto sequence_info = SequenceInfo::Get().ReadFrom(owner_->dosbus());
  // CropInfo (aka AvScratch6, aka "crop_infor")
  const auto crop_info = CropInfo::Get().ReadFrom(owner_->dosbus());
  // StreamInfo2 (aka AvScratchB, aka "param4" aka "reg_val")
  const auto stream_info2 = StreamInfo2::Get().ReadFrom(owner_->dosbus());

  // At this point, we queue some post-parsing NALUs to H264Decoder.  Specifically, SPS, PPS (TBD),
  // and slice header.  Then we call H264Decoder::Decode() which processes those queued NALUs to
  // basically catch the H264Decoder up to roughly where the HW is on the slice the HW just
  // indicated with an interrupt.
  //
  // Probably we could queue fewer SPS and PPS headers, but queuing before every picture works.
  //
  // Any "not avaialable from FW" comments below should be read as "not obviously avaialble from
  // FW, but maybe?".
  //
  // TODO(fxb/13483): Test with multi-slice pictures.

  // SPS
  //
  // This set of fields is not necessarily the minimum necessary set for this driver to work.  Nor
  // is this set of fields complete, as not all fields are available from the FW.

  auto sps_nalu = std::make_unique<media::H264NALU>();
  {  // scope sps
    ZX_DEBUG_ASSERT(!sps_nalu->data);
    ZX_DEBUG_ASSERT(!sps_nalu->size);
    // Just needs to be non-zero for SPS; not available from FW but doesn't matter.
    sps_nalu->nal_ref_idc = 1;
    sps_nalu->nal_unit_type = media::H264NALU::kSPS;
    auto sps = std::make_unique<media::H264SPS>();

    // These are what's known to be available from FW:
    sps->profile_idc = (params_.data[HardwareRenderParams::kProfileIdcMmco] >> 8) & 0xff;
    // These aren't available from FW, as far as I know:
    // constraint_set0_flag
    // constraint_set1_flag
    // constraint_set2_flag
    // constraint_set3_flag
    // constraint_set4_flag
    // constraint_set5_flag
    //
    // We'd like to have constraint_set3_flag, but the FW doesn't seem able to provide that.  In
    // H264Decoder::ProcessSPS(), this means we'll assume level == 11 instead of 9, which is
    // ok, because assuming 11 (vs 9) leads to higher limits not lower.
    sps->level_idc = params_.data[HardwareRenderParams::kLevelIdcMmco];
    sps->seq_parameter_set_id = params_.data[HardwareRenderParams::kCurrentSpsId];
    if (sps->seq_parameter_set_id >= 32) {
      LOG(ERROR, "sps->seq_parameter_set_id >= 32");
      OnFatalError();
      return;
    }
    sps->chroma_format_idc = sequence_info.chroma_format_idc();
    // These aren't available from FW:
    // separate_colour_plane_flag
    // bit_depth_luma_minus8
    // bit_depth_chroma_minus8
    // qpprime_y_zero_transform_bypass_flag
    // seq_scaling_matrix_present_flag
    // scaling_list4x4
    // scaling_list8x8
    sps->log2_max_frame_num_minus4 = params_.data[HardwareRenderParams::kLog2MaxFrameNum] - 4;
    if (sps->log2_max_frame_num_minus4 >= 13) {
      LOG(ERROR, "sps->log2_max_frame_num_minus4 >= 13");
      OnFatalError();
      return;
    }
    sps->pic_order_cnt_type = params_.data[HardwareRenderParams::kPicOrderCntType];
    sps->log2_max_pic_order_cnt_lsb_minus4 =
        params_.data[HardwareRenderParams::kLog2MaxPicOrderCntLsb] - 4;
    sps->delta_pic_order_always_zero_flag =
        params_.data[HardwareRenderParams::kDeltaPicOrderAlwaysZeroFlag];
    sps->offset_for_non_ref_pic =
        static_cast<int16_t>(params_.data[HardwareRenderParams::kOffsetForNonRefPic]);
    sps->offset_for_top_to_bottom_field =
        static_cast<int16_t>(params_.data[HardwareRenderParams::kOffsetForTopToBottomField]);
    sps->num_ref_frames_in_pic_order_cnt_cycle =
        params_.data[HardwareRenderParams::kNumRefFramesInPicOrderCntCycle];
    ZX_DEBUG_ASSERT(sps->num_ref_frames_in_pic_order_cnt_cycle >= 0);
    if (static_cast<uint32_t>(sps->num_ref_frames_in_pic_order_cnt_cycle) >
        HardwareRenderParams::kMaxNumRefFramesInPicOrderCntCycle) {
      LOG(ERROR,
          "sps->num_ref_frames_in_pic_order_cnt_cycle > kMaxNumRefFramesInPicOrderCntCycle (128) - "
          "FW supports up to 128 (not 255) - value: %d",
          sps->num_ref_frames_in_pic_order_cnt_cycle);
      OnFatalError();
      return;
    }
    // No point in setting sps->expected_delta_per_pic_order_cnt_cycle because never used.
    for (uint32_t i = 0; i < HardwareRenderParams::kMaxNumRefFramesInPicOrderCntCycle; ++i) {
      sps->offset_for_ref_frame[i] =
          static_cast<int16_t>(params_.data[HardwareRenderParams::kOffsetForRefFrameBase + i]);
    }
    sps->max_num_ref_frames = params_.data[HardwareRenderParams::kMaxReferenceFrameNum];
    ZX_DEBUG_ASSERT(static_cast<uint32_t>(sps->max_num_ref_frames) ==
                    stream_info2.max_reference_size());
    sps->gaps_in_frame_num_value_allowed_flag =
        params_.data[HardwareRenderParams::kFrameNumGapAllowed];

    ZX_DEBUG_ASSERT(hw_coded_width_ / kMacroblockDimension ==
                    params_.data[HardwareRenderParams::kMbWidth]);
    ZX_DEBUG_ASSERT(hw_coded_height_ / kMacroblockDimension ==
                    params_.data[HardwareRenderParams::kMbHeight]);
    sps->pic_width_in_mbs_minus1 = (hw_coded_width_ / kMacroblockDimension) - 1;
    // Because frame_mbs_only_flag true, we know this is in units of MBs.
    sps->pic_height_in_map_units_minus1 = (hw_coded_height_ / kMacroblockDimension) - 1;

    // Also available via SCRATCH2 during FW config request; more convenient to get this way though.
    sps->frame_mbs_only_flag = params_.data[HardwareRenderParams::kFrameMbsOnlyFlag];
    if (!sps->frame_mbs_only_flag) {
      LOG(ERROR, "!sps->frame_mbs_only_flag - not supported");
      OnFatalError();
      return;
    }
    sps->mb_adaptive_frame_field_flag = !!(params_.data[HardwareRenderParams::kMbffInfo] & 0x2);
    // ignoring direct_8x8_inference_flag - might be in kMode8x8Flags
    sps->frame_cropping_flag = (params_.data[HardwareRenderParams::kCroppingLeftRight] ||
                                params_.data[HardwareRenderParams::kCroppingTopBottom]);
    sps->frame_crop_left_offset = params_.data[HardwareRenderParams::kCroppingLeftRight] >> 8;
    sps->frame_crop_right_offset = params_.data[HardwareRenderParams::kCroppingLeftRight] & 0xff;
    sps->frame_crop_top_offset = params_.data[HardwareRenderParams::kCroppingTopBottom] >> 8;
    sps->frame_crop_bottom_offset = params_.data[HardwareRenderParams::kCroppingTopBottom] & 0xff;
    ZX_DEBUG_ASSERT(crop_info.left() == static_cast<uint32_t>(sps->frame_crop_left_offset));
    ZX_DEBUG_ASSERT(crop_info.right() == static_cast<uint32_t>(sps->frame_crop_right_offset));
    ZX_DEBUG_ASSERT(crop_info.top() == static_cast<uint32_t>(sps->frame_crop_top_offset));
    ZX_DEBUG_ASSERT(crop_info.bottom() == static_cast<uint32_t>(sps->frame_crop_bottom_offset));

    // Re. VUI, we only extract sar_width and sar_height, not any other parameters under
    // vui_parameters_present_flag, for now.  In particular we ignore bitstream_restriction_flag
    // from FW since the FW doesn't provide max_num_reorder_frames, max_dec_frame_buffering.
    //
    // TODO(dustingreen): Try to determine if data[kDpbBufferInfo] has max_num_reorder_frames and
    // max_dec_frame_buffering.
    bool aspect_ratio_info_present_flag =
        !!(params_.data[HardwareRenderParams::kVuiStatus] &
           HardwareRenderParams::kVuiStatusMaskAspectRatioInfoPresentFlag);
    // Some of the following could be shared with ParseVUIParameters() - it's not a lot of redundant
    // code though; we just need to get sar_width and sar_height filled out (or left zero, as
    // appropriate)
    ZX_DEBUG_ASSERT(!sps->sar_width);
    ZX_DEBUG_ASSERT(!sps->sar_height);
    if (aspect_ratio_info_present_flag) {
      uint16_t aspect_ratio_idc = params_.data[HardwareRenderParams::kAspectRatioIdc];
      if (aspect_ratio_idc == media::H264SPS::kExtendedSar) {
        sps->sar_width = params_.data[HardwareRenderParams::kAspectRatioSarWidth];
        sps->sar_height = params_.data[HardwareRenderParams::kAspectRatioSarHeight];
      } else {
        if (aspect_ratio_idc >= countof(kTableSarWidth)) {
          LOG(ERROR, "spect_ratio_idc >= countof(kTableSarWidth)");
          OnFatalError();
          return;
        }
        sps->sar_width = kTableSarWidth[aspect_ratio_idc];
        sps->sar_height = kTableSarHeight[aspect_ratio_idc];
      }
    }
    sps->vui_parameters_present_flag = aspect_ratio_info_present_flag;

    // We intentionally don't ever set bitstream_restriction_flag since it doesn't appear we can get
    // the sub-values from the FW:
    // max_num_reorder_frames
    // max_dec_frame_buffering
    //
    // We'd like to have max_dec_frame_buffering, but it seems the FW only provides
    // kMaxReferenceFrameNum (aka max_num_ref_frames).

    // We intentionally don't set these because they're not used:
    // timing_info_present_flag
    // num_units_in_tick
    // time_scale
    // fixed_frame_rate_flag

    // We intentionally don't set these because they're not used:
    // video_signal_type_present_flag
    // video_format
    // video_full_range_flag
    // colour_description_present_flag
    // colour_primaries
    // transfer_characteristics
    // matrix_coefficients

    // We intentionally don't set these because they're not used:
    // nal_hrd_parameters_present_flag
    // cpb_cnt_minus1
    // bit_rate_scale
    // cpb_size_scale
    // bit_rate_value_minus1
    // cpb_size_value_minus1
    // cbr_flag
    // initial_cpb_removal_delay_length_minus_1
    // cpb_removal_delay_length_minus1
    // dpb_output_delay_length_minus1
    // time_offset_length
    // low_delay_hrd_flag

    // We intentionally don't set chroma_array_type because we don't support
    // separate_colour_plane_flag true, so chroma_array_type should be 0.
    ZX_DEBUG_ASSERT(sps->chroma_array_type == 0);

    ZX_DEBUG_ASSERT(sizeof(current_sps_) == sizeof(*sps.get()));
    if (memcmp(&current_sps_, sps.get(), sizeof(current_sps_))) {
      // struct copy
      current_sps_ = *sps;
      sps_nalu->preparsed_header.emplace<std::unique_ptr<media::H264SPS>>(std::move(sps));
    } else {
      sps_nalu.reset();
    }
  }  // ~sps

  // PPS
  //
  // This set of fields is not necessarily the minimum necessary set for this driver to work.  Nor
  // is this set of fields complete, as not all fields are available from the FW.

  auto pps_nalu = std::make_unique<media::H264NALU>();
  {  // scope pps
    ZX_DEBUG_ASSERT(!pps_nalu->data);
    ZX_DEBUG_ASSERT(!pps_nalu->size);
    // Just needs to be on-zero for PPS; not available from FW but doesn't matter.
    pps_nalu->nal_ref_idc = 1;
    pps_nalu->nal_unit_type = media::H264NALU::kPPS;
    auto pps = std::make_unique<media::H264PPS>();

    pps->pic_parameter_set_id = params_.data[HardwareRenderParams::kCurrentPpsId];
    pps->seq_parameter_set_id = params_.data[HardwareRenderParams::kCurrentSpsId];
    if (pps->seq_parameter_set_id >= 32) {
      LOG(ERROR, "pps->seq_parameter_set_id >= 32");
      OnFatalError();
      return;
    }
    pps->entropy_coding_mode_flag = params_.data[HardwareRenderParams::kEntropyCodingModeFlag];
    // bottom_field_pic_order_in_frame_present_flag not available from FW
    pps->num_slice_groups_minus1 = params_.data[HardwareRenderParams::kNumSliceGroupsMinus1];
    if (pps->num_slice_groups_minus1 > 0) {
      LOG(ERROR, "pps->num_slice_groups_minus1 > 0 - not supported");
      OnFatalError();
      return;
    }
    pps->num_ref_idx_l0_default_active_minus1 =
        params_.data[HardwareRenderParams::kPpsNumRefIdxL0ActiveMinus1];
    if (pps->num_ref_idx_l0_default_active_minus1 >= 32) {
      LOG(ERROR, "pps->num_ref_idx_l0_default_active_minus1 >= 32");
      OnFatalError();
      return;
    }
    pps->num_ref_idx_l1_default_active_minus1 =
        params_.data[HardwareRenderParams::kPpsNumRefIdxL1ActiveMinus1];
    if (pps->num_ref_idx_l1_default_active_minus1 >= 32) {
      LOG(ERROR, "pps->num_ref_idx_l1_default_active_minus1 >= 32");
      OnFatalError();
      return;
    }
    pps->weighted_pred_flag = params_.data[HardwareRenderParams::kWeightedPredFlag];
    pps->weighted_bipred_idc = params_.data[HardwareRenderParams::kWeightedBipredIdc];

    // We grab this just for the error checking.
    pps->pic_init_qp_minus26 =
        static_cast<int16_t>(params_.data[HardwareRenderParams::kPicInitQpMinus26]);
    if (pps->pic_init_qp_minus26 < -26 || pps->pic_init_qp_minus26 > 25) {
      LOG(ERROR, "pps->pic_init_qp_minus26 < -26 || pps->pic_init_qp_minus26 > 25 - value: %d",
          pps->pic_init_qp_minus26);
      OnFatalError();
      return;
    }
    // pic_init_qs_minus26 not available from FW
    // chroma_qp_index_offset not available from FW
    pps->deblocking_filter_control_present_flag =
        params_.data[HardwareRenderParams::kDeblockingFilterControlPresentFlag];
    // constrained_intra_pred_flag not available from FW
    pps->redundant_pic_cnt_present_flag =
        params_.data[HardwareRenderParams::kRedundantPicCntPresentFlag];
    if (pps->redundant_pic_cnt_present_flag) {
      // Since redundant_pic_cnt isn't available from the FW, we have to assume it might be non-zero
      // and fail here instead.  It also doesn't appear on first glance that H264Decoder handles
      // non-zero redundant_pic_cnt.  The kSkipPicCount field _might_ be the redundant_pic_cnt, or
      // maybe not.
      LOG(ERROR, "pps->redundant_pic_cnt_present_flag - not supported");
      OnFatalError();
      return;
    }
    // transform_8x8_mode_flag not available from FW?
    // pic_scaling_matrix_present_flag not available from FW.
    // scaling_list4x4 not available from FW.
    // scaling_list8x8 not available from FW.
    // second_chroma_qp_index_offset not avaialble from FW.
    ZX_DEBUG_ASSERT(sizeof(current_pps_) == sizeof(*pps.get()));
    if (memcmp(&current_pps_, pps.get(), sizeof(current_pps_))) {
      // struct copy
      current_pps_ = *pps;
      pps_nalu->preparsed_header.emplace<std::unique_ptr<media::H264PPS>>(std::move(pps));
    } else {
      pps_nalu.reset();
    }
  }  // ~pps

  // SliceHeader
  auto slice_nalu = std::make_unique<media::H264NALU>();
  {  // scope slice
    ZX_DEBUG_ASSERT(!slice_nalu->data);
    ZX_DEBUG_ASSERT(!slice_nalu->size);
    slice_nalu->nal_ref_idc = params_.data[HardwareRenderParams::kNalRefIdc];
    slice_nalu->nal_unit_type = params_.data[HardwareRenderParams::kNalUnitType];
    if (slice_nalu->nal_unit_type == media::H264NALU::kCodedSliceExtension) {
      LOG(ERROR, "nal_unit_type == kCodedSliceExtension - not supported");
      OnFatalError();
      return;
    }
    auto slice = std::make_unique<media::H264SliceHeader>();
    slice->idr_pic_flag = (slice_nalu->nal_unit_type == 5);
    slice->nal_ref_idc = slice_nalu->nal_ref_idc;
    ZX_DEBUG_ASSERT(!slice->nalu_data);
    ZX_DEBUG_ASSERT(!slice->nalu_size);
    ZX_DEBUG_ASSERT(!slice->header_bit_size);
    slice->first_mb_in_slice = params_.data[HardwareRenderParams::kFirstMbInSlice];
    slice->slice_type = params_.data[HardwareRenderParams::kSliceType];
    slice->pic_parameter_set_id = params_.data[HardwareRenderParams::kCurrentPpsId];
    ZX_DEBUG_ASSERT(!slice->colour_plane_id);
    slice->frame_num = params_.data[HardwareRenderParams::kFrameNum];
    // interlaced not supported
    if (params_.data[HardwareRenderParams::kPictureStructureMmco] !=
        HardwareRenderParams::kPictureStructureMmcoFrame) {
      LOG(ERROR,
          "data[kPictureStructureMmco] != Frame - not supported - data[kPictureStructureMmco]: %x",
          params_.data[HardwareRenderParams::kPictureStructureMmco]);
      OnFatalError();
      return;
    }
    if (params_.data[HardwareRenderParams::kNewPictureStructure] !=
        HardwareRenderParams::kNewPictureStructureFrame) {
      LOG(ERROR, "data[kNewPictureStructure] != Frame - not supported");
      OnFatalError();
      return;
    }
    ZX_DEBUG_ASSERT(!slice->field_pic_flag);
    ZX_DEBUG_ASSERT(!slice->bottom_field_flag);
    slice->idr_pic_id = params_.data[HardwareRenderParams::kIdrPicId];
    slice->pic_order_cnt_lsb = params_.data[HardwareRenderParams::kPicOrderCntLsb];
    slice->delta_pic_order_cnt_bottom =
        params_.Read32(HardwareRenderParams::kDeltaPicOrderCntBottom_0);
    slice->delta_pic_order_cnt0 = params_.Read32(HardwareRenderParams::kDeltaPicOrderCnt0_0);
    slice->delta_pic_order_cnt1 = params_.Read32(HardwareRenderParams::kDeltaPicOrderCnt1_0);
    // redundant_pic_cnt not available from FW
    ZX_DEBUG_ASSERT(!slice->redundant_pic_cnt);
    // direct_spatial_mv_pred_flag not available from FW
    ZX_DEBUG_ASSERT(!slice->direct_spatial_mv_pred_flag);
    // Since num_ref_idx_active_override_flag isn't available from the FW, but the result of
    // aggregating PPS and SliceHeader is, we just pretend that the SliceHeader always overrides.
    // For all we know, it does, and there's no real benefit to avoiding the override if PPS already
    // matches, especially since we're less sure whether kPpsNumRefIdxL0ActiveMinus1 has the PPS's
    // value in the first place.
    slice->num_ref_idx_active_override_flag = true;
    slice->num_ref_idx_l0_active_minus1 =
        params_.data[HardwareRenderParams::kNumRefIdxL0ActiveMinus1];
    slice->num_ref_idx_l1_active_minus1 =
        params_.data[HardwareRenderParams::kNumRefIdxL1ActiveMinus1];
    // checked above
    ZX_DEBUG_ASSERT(slice_nalu->nal_unit_type != media::H264NALU::kCodedSliceExtension);
    // Each cmd is 2 uint16_t in src, and src has room for 33 commands so that the list of commands
    // can always be terminated by a 3.  In contrast, dst only has room for 32, and when all are
    // used there's no terminating 3.
    auto process_reorder_cmd_list = [this](const uint16_t* src_cmd_array,
                                           bool* ref_pic_list_modification_flag_lx_out,
                                           media::H264ModificationOfPicNum* dst_cmd_array) -> bool {
      ZX_DEBUG_ASSERT(src_cmd_array);
      ZX_DEBUG_ASSERT(ref_pic_list_modification_flag_lx_out);
      ZX_DEBUG_ASSERT(dst_cmd_array);
      if (src_cmd_array[0] != 3) {
        *ref_pic_list_modification_flag_lx_out = true;
        uint32_t src_index = 0;
        uint32_t dst_index = 0;
        uint32_t command;
        do {
          command = src_cmd_array[src_index];
          ZX_DEBUG_ASSERT(dst_index * 2 == src_index);
          if (dst_index >= media::H264SliceHeader::kRefListModSize) {
            // 32
            ZX_DEBUG_ASSERT(dst_index == media::H264SliceHeader::kRefListModSize);
            // 64
            ZX_DEBUG_ASSERT(src_index == HardwareRenderParams::kLxReorderCmdCount - 2);
            if (command == 3) {
              // this is actually ok, to have 32 commands with no terminating 3
              break;
            }
            LOG(ERROR, "command != 3 && dst_index == kRefListModSize");
            OnFatalError();
            return false;
          }
          if (command != 0 && command != 1 && command != 2 & command != 3) {
            LOG(ERROR, "command != 3 && command not in {0, 1, 2, 3} - out of sync with FW?");
            OnFatalError();
            return false;
          }
          ZX_DEBUG_ASSERT(dst_index <= media::H264SliceHeader::kRefListModSize - 1);
          ZX_DEBUG_ASSERT(src_index <= HardwareRenderParams::kLxReorderCmdCount - 4);
          media::H264ModificationOfPicNum& dst = dst_cmd_array[dst_index];
          ZX_DEBUG_ASSERT(command == src_cmd_array[src_index]);
          dst.modification_of_pic_nums_idc = src_cmd_array[src_index++];
          ZX_DEBUG_ASSERT(src_index <= HardwareRenderParams::kLxReorderCmdCount - 3);
          if (command == 0 || command == 1) {
            dst.abs_diff_pic_num_minus1 = src_cmd_array[src_index++];
          } else if (command == 2) {
            dst.long_term_pic_num = src_cmd_array[src_index++];
          } else {
            ZX_DEBUG_ASSERT(command == 3);
          }
          ++dst_index;
        } while (command != 3);
      } else {
        ZX_DEBUG_ASSERT(!*ref_pic_list_modification_flag_lx_out);
      }
      return true;
    };
    if (!slice->IsISlice() && !slice->IsSISlice()) {
      if (!process_reorder_cmd_list(&params_.data[HardwareRenderParams::kL0ReorderCmdBase],
                                    &slice->ref_pic_list_modification_flag_l0,
                                    &slice->ref_list_l0_modifications[0])) {
        // OnFatalError() already called
        return;
      }
    }
    if (slice->IsBSlice()) {
      if (!process_reorder_cmd_list(&params_.data[HardwareRenderParams::kL1ReorderCmdBase],
                                    &slice->ref_pic_list_modification_flag_l1,
                                    &slice->ref_list_l1_modifications[0])) {
        // OnFatalError() already called
        return;
      }
    }
    // These don't appear to be available from FW:
    // luma_log2_weight_denom
    // chroma_log2_weight_denom
    // luma_weight_l0_flag
    // chroma_weight_l0_flag
    // pred_weight_table_l0
    // luma_weight_l1_flag
    // chroma_weight_l1_flag
    // pred_weight_table_l1
    if (slice->IsISlice()) {
      slice->no_output_of_prior_pics_flag =
          !!(params_.data[HardwareRenderParams::kMmcoCmd + 0] & 0x2);
      slice->long_term_reference_flag = !!(params_.data[HardwareRenderParams::kMmcoCmd + 0] & 0x1);
    }
    if (slice_nalu->nal_ref_idc) {
      uint32_t src_index = 0;
      uint32_t dst_index = 0;
      uint16_t* mmco_cmds = &params_.data[HardwareRenderParams::kMmcoCmd];
      constexpr uint32_t kSrcMmcoCmdCount = 44;
      // Probably 32 is enough for most streams, but unclear if 32 is really a limit in the h264
      // spec.
      constexpr uint32_t kDstMmcoCmdCount = media::H264SliceHeader::kRefListSize;
      while (true) {
        if (src_index >= kSrcMmcoCmdCount) {
          LOG(ERROR, "src_index >= kSrcMmcoCmdCount - unsupported stream");
          OnFatalError();
          return;
        }
        if (dst_index >= kDstMmcoCmdCount) {
          LOG(ERROR, "dst_index >= kDstMmcoCmdCount - unsupported stream");
          OnFatalError();
          return;
        }
        uint16_t mmco = mmco_cmds[src_index++];
        if (mmco > 6) {
          LOG(ERROR, "mmco > 6");
          OnFatalError();
          return;
        }
        media::H264DecRefPicMarking& dst = slice->ref_pic_marking[dst_index];
        dst.memory_mgmnt_control_operation = mmco;
        if (mmco == 0) {
          break;
        }
        // We need at least enough room to read mmco == 0 next loop iteration, if not something else
        // sooner.
        if (src_index >= kSrcMmcoCmdCount) {
          LOG(ERROR, "src_index >= kSrcMmcoCmdCount - unsupported stream");
          OnFatalError();
          return;
        }
        slice->adaptive_ref_pic_marking_mode_flag = true;
        if (mmco == 1 || mmco == 3) {
          dst.difference_of_pic_nums_minus1 = mmco_cmds[src_index++];
        } else if (mmco == 2) {
          dst.long_term_pic_num = mmco_cmds[src_index++];
        }
        // We need at least enough room to read mmco == 0 next loop iteration, if not something else
        // sooner.
        if (src_index >= kSrcMmcoCmdCount) {
          LOG(ERROR, "src_index >= kSrcMmcoCmdCount - unsupported stream");
          OnFatalError();
          return;
        }
        if (mmco == 3 || mmco == 6) {
          dst.long_term_frame_idx = mmco_cmds[src_index++];
        } else if (mmco == 4) {
          dst.max_long_term_frame_idx_plus1 = mmco_cmds[src_index++];
        }
        ++dst_index;
        // src_index is checked first thing at top of loop
      }
      // Must end up 0 terminated, or we already failed above.  This comment is not intending to
      // imply that a stream with more mmco commands is necessarily invalid (TBD - h264 spec seems
      // a bit vague on how many there can be).
      ZX_DEBUG_ASSERT(dst_index < kDstMmcoCmdCount &&
                      slice->ref_pic_marking[dst_index].memory_mgmnt_control_operation == 0);
    }
    // Not available from FW:
    // cabac_init_idc
    // slice_qp_delta
    // sp_for_switch_flag
    // slice_qs_delta
    // disable_deblocking_filter_idc
    // slice_alpha_c0_offset_div2
    // slice_beta_offset_div2

    // These are set but never read in H264Decoder, so don't need to set them:
    // dec_ref_pic_marking_bit_size
    // pic_order_cnt_bit_size
    slice_nalu->preparsed_header.emplace<std::unique_ptr<media::H264SliceHeader>>(std::move(slice));
  }  // ~slice

  if (sps_nalu) {
    media_decoder_->QueuePreparsedNalu(std::move(sps_nalu));
  }
  if (pps_nalu) {
    media_decoder_->QueuePreparsedNalu(std::move(pps_nalu));
  }
  media_decoder_->QueuePreparsedNalu(std::move(slice_nalu));

  media::AcceleratedVideoDecoder::DecodeResult decode_result;
  bool decode_done = false;
  while (!decode_done) {
    decode_result = media_decoder_->Decode();
    switch (decode_result) {
      case media::AcceleratedVideoDecoder::kDecodeError:
        LOG(ERROR, "kDecodeError");
        OnFatalError();
        return;
      case media::AcceleratedVideoDecoder::kConfigChange:
        // TODO: verify that the config change is a NOP vs. the previous ConfigureDpb().
        continue;
      case media::AcceleratedVideoDecoder::kRanOutOfStreamData:
        decode_done = true;
        break;
      case media::AcceleratedVideoDecoder::kRanOutOfSurfaces:
        // The pre-check in PumpDecoder() is intended to prevent this from happening.  If this were
        // to happen, it'd very likely disrupt progress of any concurrent stream, since swapping out
        // at a slice header isn't implemented so far (unknown whether saving/restoring state at a
        // slice header is possible).
        LOG(ERROR, "kRanOutOfSurfaces despite pre-check in PumpDecoder()");
        OnFatalError();
        ZX_DEBUG_ASSERT(!IsUnusedReferenceFrameAvailable());
        return;
      case media::AcceleratedVideoDecoder::kNeedContextUpdate:
        LOG(ERROR, "kNeedContextUpdate is impossible");
        OnFatalError();
        return;
      case media::AcceleratedVideoDecoder::kTryAgain:
        LOG(ERROR, "kTryAgain despite this accelerator never indicating that");
        OnFatalError();
        return;
      default:
        LOG(ERROR, "unexpected decode_result: %u", decode_result);
        OnFatalError();
        return;
    }
  }
  ZX_DEBUG_ASSERT(decode_result == media::AcceleratedVideoDecoder::kRanOutOfStreamData);

  ZX_DEBUG_ASSERT(state_ == DecoderState::kRunning);

  // Configure the HW and decode the body of the slice.
  current_frame_ = current_metadata_frame_;
  // We fed the media_decoder_ with pre-parsed SPS, PPS, SliceHeader, so the decoder will have
  // indicated exactly 1 slice (or possibly indicated failure above).
  ZX_DEBUG_ASSERT(slice_data_list_.size() == 1);
  SliceData slice_data = std::move(slice_data_list_.front());
  slice_data_list_.pop_front();
  ZX_DEBUG_ASSERT(slice_data_list_.empty());

  // The following checks are to try to ensure what the hardware's parsing matches what H264Decoder
  // processed from sps_nalu, pps_nalu, slice_nalu.

  // Slices 5-9 are equivalent for this purpose with slices 0-4 - see 7.4.3
  constexpr uint32_t kSliceTypeMod = 5;
  ZX_DEBUG_ASSERT(slice_data.header.slice_type % kSliceTypeMod ==
                  params_.data[HardwareRenderParams::kSliceType] % kSliceTypeMod);

  // Check for interlacing (already rejected above).
  constexpr uint32_t kPictureStructureFrame = 3;
  ZX_DEBUG_ASSERT(params_.data[HardwareRenderParams::kNewPictureStructure] ==
                  kPictureStructureFrame);

  auto poc = poc_.ComputePicOrderCnt(&slice_data.sps, slice_data.header);
  if (!poc) {
    LOG(ERROR, "No poc");
    OnFatalError();
    return;
  }
  DLOG("Frame POC %d", poc.value());

  H264CurrentPocIdxReset::Get().FromValue(0).WriteTo(owner_->dosbus());
  // Assume all fields have the same POC, since the chromium code doesn't support interlacing.
  // frame
  H264CurrentPoc::Get().FromValue(poc.value()).WriteTo(owner_->dosbus());
  // top field
  H264CurrentPoc::Get().FromValue(poc.value()).WriteTo(owner_->dosbus());
  // bottom field
  H264CurrentPoc::Get().FromValue(poc.value()).WriteTo(owner_->dosbus());
  CurrCanvasCtrl::Get()
      .FromValue(0)
      .set_canvas_index(current_frame_->index)
      .WriteTo(owner_->dosbus());
  // Unclear if reading from the register is actually necessary, or if this
  // would always be the same as above.
  uint32_t curr_canvas_index =
      CurrCanvasCtrl::Get().ReadFrom(owner_->dosbus()).lower_canvas_index();
  RecCanvasCtrl::Get().FromValue(curr_canvas_index).WriteTo(owner_->dosbus());
  DbkrCanvasCtrl::Get().FromValue(curr_canvas_index).WriteTo(owner_->dosbus());
  DbkwCanvasCtrl::Get().FromValue(curr_canvas_index).WriteTo(owner_->dosbus());

  // Info for a progressive frame.
  constexpr uint32_t kProgressiveFrameInfo = 0xf480;
  current_frame_->info0 = kProgressiveFrameInfo;
  // Top field
  current_frame_->info1 = poc.value();
  // Bottom field
  current_frame_->info2 = poc.value();
  current_frame_->is_long_term_reference = slice_data.pic->long_term;

  H264BufferInfoIndex::Get().FromValue(16).WriteTo(owner_->dosbus());

  // Store information about the properties of each canvas image.
  for (uint32_t i = 0; i < video_frames_.size(); ++i) {
    bool is_long_term = video_frames_[i]->is_long_term_reference;
    if (is_long_term) {
      // Everything is progressive, so mark as having both bottom and top as long-term references.
      constexpr uint32_t kTopFieldLongTerm = 1 << 4;
      constexpr uint32_t kBottomFieldLongTerm = 1 << 5;
      video_frames_[i]->info0 |= kTopFieldLongTerm | kBottomFieldLongTerm;
    }
    uint32_t info_to_write = video_frames_[i]->info0;
    if (video_frames_[i].get() == current_frame_) {
      constexpr uint32_t kCurrentFrameBufInfo = 0xf;
      info_to_write |= kCurrentFrameBufInfo;
    }
    ZX_DEBUG_ASSERT(video_frames_[i]->index == i);
    H264BufferInfoData::Get().FromValue(info_to_write).WriteTo(owner_->dosbus());
    H264BufferInfoData::Get().FromValue(video_frames_[i]->info1).WriteTo(owner_->dosbus());
    H264BufferInfoData::Get().FromValue(video_frames_[i]->info2).WriteTo(owner_->dosbus());
  }
  if (!InitializeRefPics(slice_data.ref_pic_list0, 0))
    return;
  if (!InitializeRefPics(slice_data.ref_pic_list1, 8))
    return;

  // Wait for the hardware to finish processing its current mbs.  Normally this should be quick, but
  // wait a while to avoid potential spurious timeout (none observed at 100ms).
  if (!SpinWaitForRegister(std::chrono::milliseconds(400), [&] {
        return !H264CoMbRwCtl::Get().ReadFrom(owner_->dosbus()).busy();
      })) {
    LOG(ERROR, "Failed to wait for rw register nonbusy");
    OnFatalError();
    return;
  }

  constexpr uint32_t kMvRefDataSizePerMb = 96;
  uint32_t mv_size = kMvRefDataSizePerMb;

  if ((params_.data[HardwareRenderParams::kMode8x8Flags] & 4) &&
      (params_.data[HardwareRenderParams::kMode8x8Flags] & 2)) {
    // direct 8x8 mode seems to store 1/4 the data, so the offsets need to be less as well.
    mv_size /= 4;
  }
  uint32_t mv_byte_offset = slice_data.header.first_mb_in_slice * mv_size;

  H264CoMbWrAddr::Get()
      .FromValue(truncate_to_32(current_frame_->reference_mv_buffer.phys_base()) + mv_byte_offset)
      .WriteTo(owner_->dosbus());

  // 8.4.1.2.1 - co-located motion vectors come from RefPictList1[0] for frames.
  if (slice_data.ref_pic_list1.size() > 0) {
    auto* amlogic_picture = static_cast<AmlogicH264Picture*>(slice_data.ref_pic_list1[0].get());
    if (amlogic_picture) {
      auto internal_picture = amlogic_picture->internal_picture.lock();
      if (!internal_picture) {
        DECODE_ERROR("Co-mb read buffer nonexistent");
        frame_data_provider_->AsyncResetStreamAfterCurrentFrame();
        return;
      }
      uint32_t read_addr =
          truncate_to_32(internal_picture->reference_mv_buffer.phys_base()) + mv_byte_offset;
      ZX_DEBUG_ASSERT(read_addr % 8 == 0);
      H264CoMbRdAddr::Get().FromValue((read_addr >> 3) | (2u << 30)).WriteTo(owner_->dosbus());
    }
  }

  // TODO: Maybe we could do what H264Decoder::IsNewPrimaryCodedPicture() does to detect this, but
  // this seems to work for now, and I'm not aware of any specific cases where it doesn't work.
  if (slice_data.header.first_mb_in_slice == 0) {
    DpbStatusReg::Get().FromValue(kH264ActionDecodeNewpic).WriteTo(owner_->dosbus());
  } else {
    DpbStatusReg::Get().FromValue(kH264ActionDecodeSlice).WriteTo(owner_->dosbus());
  }
  owner_->watchdog()->Start();
}

// not currently used
void H264MultiDecoder::FlushFrames() {
  auto res = media_decoder_->Flush();
  DLOG("Got media decoder res %d", res);
}

void H264MultiDecoder::DumpStatus() {
  DLOG("ViffBitCnt: %d", ViffBitCnt::Get().ReadFrom(owner_->dosbus()).reg_value());
  DLOG("Viifolevel: %d", VldMemVififoLevel::Get().ReadFrom(owner_->dosbus()).reg_value());
  DLOG("input offset: %d read offset: %d", owner_->core()->GetStreamInputOffset(),
       owner_->core()->GetReadOffset());
  DLOG("Error status reg %d mbymbx reg %d",
       ErrorStatusReg::Get().ReadFrom(owner_->dosbus()).reg_value(),
       MbyMbx::Get().ReadFrom(owner_->dosbus()).reg_value());
  DLOG("DpbStatusReg 0x%x", DpbStatusReg::Get().ReadFrom(owner_->dosbus()).reg_value());
}

void H264MultiDecoder::HandlePicDataDone() {
  ZX_DEBUG_ASSERT(current_frame_);
  owner_->watchdog()->Cancel();
  current_frame_ = nullptr;
  current_metadata_frame_ = nullptr;

  // Bring the decoder into sync that the frame is done decoding.  This way media_decoder_ can
  // output frames and do post-decode DPB or MMCO updates.  This pushes media_decoder_ from
  // searching for NAL end (pre-frame-decode) to post-frame-decode and post-any-frames-output.
  auto aud_nalu = std::make_unique<media::H264NALU>();
  ZX_DEBUG_ASSERT(!aud_nalu->data);
  ZX_DEBUG_ASSERT(!aud_nalu->size);
  aud_nalu->nal_ref_idc = 0;
  aud_nalu->nal_unit_type = media::H264NALU::kAUD;
  media_decoder_->QueuePreparsedNalu(std::move(aud_nalu));
  media::AcceleratedVideoDecoder::DecodeResult decode_result = media_decoder_->Decode();
  switch (decode_result) {
    case media::AcceleratedVideoDecoder::kDecodeError:
      LOG(ERROR, "kDecodeError");
      OnFatalError();
      return;
    case media::AcceleratedVideoDecoder::kConfigChange:
      LOG(ERROR, "kConfigChange unexpected here");
      OnFatalError();
      return;
    case media::AcceleratedVideoDecoder::kRanOutOfStreamData:
      // keep going
      break;
    case media::AcceleratedVideoDecoder::kRanOutOfSurfaces:
      LOG(ERROR, "kRanOutOfSurfaces desipte checking in advance of starting frame decode");
      OnFatalError();
      return;
    case media::AcceleratedVideoDecoder::kNeedContextUpdate:
      LOG(ERROR, "kNeedContextUpdate is impossible");
      OnFatalError();
      return;
    case media::AcceleratedVideoDecoder::kTryAgain:
      LOG(ERROR, "kTryAgain despite this accelerator never indicating that");
      OnFatalError();
      return;
  }

  OutputReadyFrames();

  // Set currently_decoding_ to false _after_ OutputReadyFrames to avoid running body of PumpDecoder
  // too early.
  state_ = DecoderState::kWaitingForInputOrOutput;
  owner_->core()->StopDecoding();
  currently_decoding_ = false;

  ZX_DEBUG_ASSERT(slice_data_list_.empty());

  owner_->TryToReschedule();
  if (state_ == DecoderState::kWaitingForInputOrOutput) {
    PumpDecoder();
  }
}

void H264MultiDecoder::OutputReadyFrames() {
  while (!frames_to_output_.empty()) {
    uint32_t index = frames_to_output_.front();
    frames_to_output_.pop_front();
    client_->OnFrameReady(video_frames_[index]->frame);
  }
}

void H264MultiDecoder::HandleHardwareError() {
  owner_->watchdog()->Cancel();
  owner_->core()->StopDecoding();
  // We need to reset the hardware here or for some malformed hardware streams (e.g.
  // bear_h264[638] = 44) the CPU will hang when trying to isolate VDEC1 power on shutdown.
  ResetHardware();
  LOG(ERROR, "ResetHardware() done.");
  frame_data_provider_->AsyncResetStreamAfterCurrentFrame();
}

void H264MultiDecoder::HandleInterrupt() {
  ZX_DEBUG_ASSERT(owner_->IsDecoderCurrent(this));
  // Clear interrupt
  VdecAssistMbox1ClrReg::Get().FromValue(1).WriteTo(owner_->dosbus());
  uint32_t decode_status = DpbStatusReg::Get().ReadFrom(owner_->dosbus()).reg_value();
  TRACE_DURATION("media", "H264MultiDecoder::HandleInterrupt", "decode_status", decode_status);
  DLOG("Got H264MultiDecoder::HandleInterrupt, decode status: %x", decode_status);
  switch (decode_status) {
    case kH264ConfigRequest: {
      DpbStatusReg::Get().FromValue(kH264ActionConfigDone).WriteTo(owner_->dosbus());
      ConfigureDpb();
      break;
    }
    case kH264DataRequest:
      DECODE_ERROR("Got unhandled data request");
      HandleHardwareError();
      break;
    case kH264SliceHeadDone: {
      HandleSliceHeadDone();
      break;
    }
    case kH264PicDataDone: {
      HandlePicDataDone();
      break;
    }
    case kH264SearchBufempty:
      // This can happen if non-slice NALU(s) show up in a packet without any slice NALU(s).
      state_ = DecoderState::kWaitingForInputOrOutput;
      owner_->watchdog()->Cancel();
      owner_->core()->StopDecoding();
      currently_decoding_ = false;
      PumpDecoder();
      break;
    case kH264DecodeTimeout:
      DECODE_ERROR("Decoder got kH264DecodeTimeout");
      HandleHardwareError();
      break;
  }
}

void H264MultiDecoder::PumpOrReschedule() {
  if (state_ == DecoderState::kSwappedOut) {
    owner_->TryToReschedule();
    // TryToReschedule will pump the decoder (using SwappedIn) once the decoder is finally
    // rescheduled.
  } else {
    is_async_pump_pending_ = false;
    PumpDecoder();
  }
}

void H264MultiDecoder::ReturnFrame(std::shared_ptr<VideoFrame> frame) {
  DLOG("H264MultiDecoder::ReturnFrame %d", frame->index);
  ZX_DEBUG_ASSERT(frame->index < video_frames_.size());
  ZX_DEBUG_ASSERT(video_frames_[frame->index]->frame == frame);
  video_frames_[frame->index]->in_use = false;
  waiting_for_surfaces_ = false;
  PumpOrReschedule();
}

void H264MultiDecoder::CallErrorHandler() { OnFatalError(); }

void H264MultiDecoder::InitializedFrames(std::vector<CodecFrame> frames, uint32_t coded_width,
                                         uint32_t coded_height, uint32_t stride) {
  DLOG("H264MultiDecoder::InitializedFrames");
  // not swapped out, not running
  ZX_DEBUG_ASSERT(state_ == DecoderState::kWaitingForConfigChange);
  uint32_t frame_count = frames.size();
  ZX_DEBUG_ASSERT(video_frames_.empty());
  for (uint32_t i = 0; i < frame_count; ++i) {
    auto frame = std::make_shared<VideoFrame>();
    // While we'd like to pass in IO_BUFFER_CONTIG, since we know the VMO was
    // allocated with zx_vmo_create_contiguous(), the io_buffer_init_vmo()
    // treats that flag as an invalid argument, so instead we have to pretend as
    // if it's a non-contiguous VMO, then validate that the VMO is actually
    // contiguous later in aml_canvas_config() called by
    // owner_->ConfigureCanvas() below.
    zx_status_t status =
        io_buffer_init_vmo(&frame->buffer, owner_->bti()->get(),
                           frames[i].buffer_spec().vmo_range.vmo().get(), 0, IO_BUFFER_RW);
    if (status != ZX_OK) {
      DECODE_ERROR("Failed to io_buffer_init_vmo() for frame - status: %d\n", status);
      OnFatalError();
      return;
    }
    io_buffer_cache_flush(&frame->buffer, 0, io_buffer_size(&frame->buffer, 0));

    BarrierAfterFlush();

    frame->hw_width = coded_width;
    frame->hw_height = coded_height;
    frame->coded_width = coded_width;
    frame->coded_height = coded_height;
    frame->stride = stride;
    frame->uv_plane_offset = stride * coded_height;
    frame->display_width = pending_display_width_;
    frame->display_height = pending_display_height_;
    frame->index = i;

    // can be nullptr
    frame->codec_buffer = frames[i].buffer_ptr();
    if (frames[i].buffer_ptr()) {
      frames[i].buffer_ptr()->SetVideoFrame(frame);
    }

    // The ConfigureCanvas() calls validate that the VMO is physically
    // contiguous, regardless of how the VMO was created.
    auto y_canvas =
        owner_->ConfigureCanvas(&frame->buffer, 0, frame->stride, frame->coded_height, 0, 0);
    auto uv_canvas = owner_->ConfigureCanvas(&frame->buffer, frame->uv_plane_offset, frame->stride,
                                             frame->coded_height / 2, 0, 0);
    if (!y_canvas || !uv_canvas) {
      OnFatalError();
      return;
    }

    // FWIW, this is the leading candidate for what StreamInfo::insignificant() bit would control,
    // but 96 works fine here regardless.  If insignificant() is 1, 24 (maybe), else 96.  Or just
    // 96 always is fine.  This speculative association could be wrong (and/or obsolete) in the
    // first place, so just use 96 here.
    constexpr uint32_t kMvRefDataSizePerMb = 96;

    uint32_t mb_width = coded_width / 16;
    uint32_t mb_height = coded_height / 16;
    uint32_t colocated_buffer_size =
        fbl::round_up(mb_width * mb_height * kMvRefDataSizePerMb, ZX_PAGE_SIZE);

    auto create_result =
        InternalBuffer::Create("H264ReferenceMvs", &owner_->SysmemAllocatorSyncPtr(), owner_->bti(),
                               colocated_buffer_size, is_secure_,
                               /*is_writable=*/true, /*is_mapping_needed*/ false);
    if (!create_result.is_ok()) {
      LOG(ERROR, "Couldn't allocate reference mv buffer - status: %d", create_result.error());
      OnFatalError();
      return;
    }

    video_frames_.push_back(std::shared_ptr<ReferenceFrame>(
        new ReferenceFrame{false, false, i, std::move(frame), std::move(y_canvas),
                           std::move(uv_canvas), create_result.take_value()}));
  }

  for (auto& frame : video_frames_) {
    AncNCanvasAddr::Get(frame->index)
        .FromValue((frame->uv_canvas->index() << 16) | (frame->uv_canvas->index() << 8) |
                   (frame->y_canvas->index()))
        .WriteTo(owner_->dosbus());
  }

  hw_coded_width_ = coded_width;
  hw_coded_height_ = coded_height;
  hw_stride_ = stride;
  // We pretend like these are configured in the HW even though they're not really.
  hw_display_width_ = pending_display_width_;
  hw_display_height_ = pending_display_height_;

  ZX_DEBUG_ASSERT(currently_decoding_);
  waiting_for_surfaces_ = false;
  state_ = DecoderState::kRunning;
  // this tells hw to go - currently_decoding_ is still true
  AvScratch0::Get()
      .FromValue(static_cast<uint32_t>((next_max_reference_size_ << 24) |
                                       (video_frames_.size() << 16) | (video_frames_.size() << 8)))
      .WriteTo(owner_->dosbus());
  owner_->watchdog()->Start();
}

void H264MultiDecoder::SubmitFrameMetadata(ReferenceFrame* reference_frame,
                                           const media::H264SPS* sps, const media::H264PPS* pps,
                                           const media::H264DPB& dpb) {
  current_metadata_frame_ = reference_frame;
}

void H264MultiDecoder::SubmitSliceData(SliceData data) {
  // Only queue up data in a list instead of starting the decode in hardware. We could try to submit
  // it now, but that makes it more difficult to swap out if we only receive data for a partial
  // frame from the client and would want to try to swap out between slices.
  slice_data_list_.push_back(data);
}

void H264MultiDecoder::OutputFrame(ReferenceFrame* reference_frame, uint32_t pts_id) {
  ZX_DEBUG_ASSERT(reference_frame->in_use);
  auto it = id_to_pts_map_.find(pts_id);
  if (it != id_to_pts_map_.end()) {
    reference_frame->frame->has_pts = true;
    reference_frame->frame->pts = it->second;
    id_to_pts_map_.erase(it);
  } else {
    LOG(INFO, "NO PTS FOR ID: %u", pts_id);
  }
#if 0
  // TODO(fxb/13483): Re-plumb this (not necessarily here).
  auto sar_size = media_decoder_->GetSarSize();
  if (sar_size.width() > 0 && sar_size.height() > 0) {
    has_sar = true;
    sar_width = sar_size.width();
    sar_height = sar_size.height();
  }
#endif
  frames_to_output_.push_back(reference_frame->index);
  // Don't output a frame that's currently being decoded into, and don't output frames out of order
  // if one's already been queued up.
  if ((frames_to_output_.size() == 1) && (current_metadata_frame_ != reference_frame)) {
    OutputReadyFrames();
  }
}

void H264MultiDecoder::SubmitDataToHardware(const uint8_t* data, size_t length,
                                            const CodecBuffer* codec_buffer,
                                            uint32_t buffer_start_offset) {
  ZX_DEBUG_ASSERT(owner_->IsDecoderCurrent(this));
  zx_paddr_t phys_addr{};
  ZX_DEBUG_ASSERT(!phys_addr);
  if (codec_buffer) {
    ZX_DEBUG_ASSERT(codec_buffer->is_known_contiguous());
    phys_addr = codec_buffer->physical_base() + buffer_start_offset;
  }
  if (use_parser_) {
    zx_status_t status =
        owner_->SetProtected(VideoDecoder::Owner::ProtectableHardwareUnit::kParser, is_secure_);
    if (status != ZX_OK) {
      LOG(ERROR, "video_->SetProtected(kParser) failed - status: %d", status);
      OnFatalError();
      return;
    }
    // Pass nullptr because we'll handle syncing updates manually.
    status = owner_->parser()->InitializeEsParser(nullptr);
    if (status != ZX_OK) {
      DECODE_ERROR("InitializeEsParser failed - status: %d", status);
      OnFatalError();
      return;
    }
    if (length > owner_->GetStreamBufferEmptySpace()) {
      // We don't want the parser to hang waiting for output buffer space, since new space will
      // never be released to it since we need to manually update the read pointer. TODO(fxb/13483):
      // Handle copying only as much as can fit and waiting for kH264DataRequest to continue
      // copying the remainder.
      DECODE_ERROR("Empty space in stream buffer %d too small for video data (%lu)",
                   owner_->GetStreamBufferEmptySpace(), length);
      OnFatalError();
      return;
    }
    owner_->parser()->SyncFromDecoderInstance(owner_->current_instance());
    DLOG("data: %p phys_addr: %p length: %zx buffer_start_offset: %u", data, phys_addr, length,
         buffer_start_offset);
    if (phys_addr) {
      status = owner_->parser()->ParseVideoPhysical(phys_addr, length);
    } else {
      status = owner_->parser()->ParseVideo(data, length);
    }
    if (status != ZX_OK) {
      DECODE_ERROR("Parsing video failed - status: %d", status);
      OnFatalError();
      return;
    }
    status = owner_->parser()->WaitForParsingCompleted(ZX_SEC(10));
    if (status != ZX_OK) {
      DECODE_ERROR("Parsing video timed out - status: %d", status);
      owner_->parser()->CancelParsing();
      OnFatalError();
      return;
    }

    owner_->parser()->SyncToDecoderInstance(owner_->current_instance());
  } else {
    zx_status_t status = owner_->ProcessVideoNoParser(data, length);
    if (status != ZX_OK) {
      DECODE_ERROR("Failed to write video");
      OnFatalError();
    }
  }
}

bool H264MultiDecoder::CanBeSwappedIn() {
  ZX_DEBUG_ASSERT(!in_pump_decoder_);
  ZX_DEBUG_ASSERT(!is_async_pump_pending_);
  if (fatal_error_) {
    return false;
  }
  if (sent_output_eos_to_client_) {
    return false;
  }
  if (waiting_for_surfaces_) {
    return false;
  }
  if (waiting_for_input_) {
    return false;
  }
  if (!video_frames_.empty() && !IsUnusedReferenceFrameAvailable()) {
    waiting_for_surfaces_ = true;
    return false;
  }
  if (!frame_data_provider_->HasMoreInputData()) {
    waiting_for_input_ = true;
    return false;
  }
  return true;
}

bool H264MultiDecoder::CanBeSwappedOut() const {
  // TODO(fxb/13483): kWaitingForConfigChange ideally would allow swapping out decoder; VP9 doesn't
  // yet either, so punt for the moment.
  return !is_async_pump_pending_ && state_ == DecoderState::kWaitingForInputOrOutput;
}

void H264MultiDecoder::SetSwappedOut() {
  ZX_DEBUG_ASSERT(!is_async_pump_pending_);
  ZX_DEBUG_ASSERT(state_ == DecoderState::kWaitingForInputOrOutput);
  ZX_DEBUG_ASSERT(CanBeSwappedOut());
  state_ = DecoderState::kSwappedOut;
}

void H264MultiDecoder::SwappedIn() {
  // Ensure at least one PumpDecoder() before swapping out again.
  //
  // Also, don't pump decoder A synchronously here because we may already be in PumpDecoder() of a
  // different decoder B presently.  This avoids being in PumpDecoder() of more than one decoder
  // at the same time (on the same stack), and avoids re-entering PumpDecoder() of the same decoder.
  is_async_pump_pending_ = true;
  frame_data_provider_->AsyncPumpDecoder();
}

void H264MultiDecoder::OnSignaledWatchdog() {
  DECODE_ERROR("Hit watchdog");
  HandleHardwareError();
}

void H264MultiDecoder::OnFatalError() {
  if (!fatal_error_) {
    fatal_error_ = true;
    client_->OnError();
  }
}

void H264MultiDecoder::ReceivedNewInput() {
  waiting_for_input_ = false;
  PumpOrReschedule();
}

void H264MultiDecoder::QueueInputEos() {
  ZX_DEBUG_ASSERT(!input_eos_queued_);
  input_eos_queued_ = true;
  ZX_DEBUG_ASSERT(in_pump_decoder_);
  ZX_DEBUG_ASSERT(!sent_output_eos_to_client_);
  ZX_DEBUG_ASSERT(!frame_data_provider_->HasMoreInputData());
  ZX_DEBUG_ASSERT(!currently_decoding_);
  if (!media_decoder_->Flush()) {
    LOG(ERROR, "Flush failed");
    OnFatalError();
    return;
  }
  sent_output_eos_to_client_ = true;
  client_->OnEos();
}

void H264MultiDecoder::PropagatePotentialEos() {}

void H264MultiDecoder::RequestStreamReset() {
  fatal_error_ = true;
  LOG(ERROR, "fatal_error_ = true");
  frame_data_provider_->AsyncResetStreamAfterCurrentFrame();
  owner_->TryToReschedule();
}

void H264MultiDecoder::PumpDecoder() {
  TRACE_DURATION("media", "H264MultiDecoder::PumpDecoder");
  ZX_DEBUG_ASSERT(!in_pump_decoder_);
  in_pump_decoder_ = true;
  auto set_not_in_pump_decoder = fit::defer([this] { in_pump_decoder_ = false; });

  if (waiting_for_surfaces_ || waiting_for_input_ || currently_decoding_ ||
      (state_ == DecoderState::kSwappedOut) || fatal_error_) {
    owner_->TryToReschedule();
    return;
  }

  // Don't start the HW decoding a frame until we know we'll be able to fairly quickly get an
  // empty frame to decode into.
  if (!video_frames_.empty() && !IsUnusedReferenceFrameAvailable()) {
    waiting_for_surfaces_ = true;
    owner_->TryToReschedule();
    return;
  }

  // Now we try to get some input data.
  std::optional<DataInput> current_data_input = frame_data_provider_->ReadMoreInputData();
  if (!current_data_input) {
    waiting_for_input_ = true;
    owner_->TryToReschedule();
    return;
  }

  auto& current_input = current_data_input.value();
  if (current_input.is_eos) {
    QueueInputEos();
    owner_->TryToReschedule();
    return;
  }

  ZX_DEBUG_ASSERT(!current_input.is_eos);
  ZX_DEBUG_ASSERT(current_input.data.empty() == !!current_input.codec_buffer);
  ZX_DEBUG_ASSERT(current_input.length != 0);
  if (current_input.pts) {
    id_to_pts_map_[next_pts_id_] = current_input.pts.value();
  }

  // We'll call QueuePreparsedNalu() and Decode() during slice header done interrupt, but we can
  // SetStreamId() here since it's convenient to do here and it's ok to set here even if we get
  // 0-N slice headers in a packet since we have a max of one PTS per packet and it's ok for these
  // IDs to be sparse.
  media_decoder_->SetStreamId(next_pts_id_);

  // Make this cycle in a reasonable way for int32_t.  Probably H264Decoder should change to use
  // uint32_t for this, but it won't come up in practice because 2^31 doesn't overflow for longer
  // than any plausible stream.  This handling is likely not enough on its own to achieve correct
  // wrapping behavior overall (yet).
  if (next_pts_id_ == std::numeric_limits<int32_t>::max()) {
    next_pts_id_ = 0;
  } else {
    ++next_pts_id_;
  }

  // Now we can submit all the data of this AU/packet plus padding to the HW decoder and start it
  // decoding.  We know (at least for now), that the packet boundary doesn't split a NALU, and
  // doesn't split an encoded frame either.  For now, this is similar to VP9 decode on this HW
  // where a whole VP9 superframe has to be in a physically contiguous packet.
  //
  // In future we may need to allow a packet boundary to separate the slices of a multi-slice
  // frame at NALU boundary.  In future we may need to pay attention to known_end_access_unit
  // instead of assuming it is true.  We may need to allow split NALUs.  We may need to allow
  // context switching any time we're not actively decoding which in future could be in the middle
  // of an AU that splits across multiple packets.  At the moment none of these are supported.
  constexpr uint8_t kHeader[] = {0, 0, 1};
  SubmitDataToHardware(kHeader, sizeof(kHeader), nullptr, 0);
  SubmitDataToHardware(current_input.data.data(), current_input.length, current_input.codec_buffer,
                       current_input.buffer_start_offset);
  SubmitDataToHardware(kPadding, kPaddingSize, nullptr, 0);

  // After this, we'll see an interrupt from the HW, either slice header done or out of data.
  StartFrameDecode();

  // ~current_data_input recycles input packet
}

bool H264MultiDecoder::IsUnusedReferenceFrameAvailable() {
  auto frame = GetUnusedReferenceFrame();
  if (!frame) {
    return false;
  }
  // put back - maybe not ideal, but works for now
  frame->in_use = false;
  frame->in_internal_use = false;
  return true;
}

std::shared_ptr<H264MultiDecoder::ReferenceFrame> H264MultiDecoder::GetUnusedReferenceFrame() {
  ZX_DEBUG_ASSERT(state_ != DecoderState::kWaitingForConfigChange);
  for (auto& frame : video_frames_) {
    ZX_DEBUG_ASSERT(frame->frame->coded_width ==
                    static_cast<uint32_t>(media_decoder_->GetPicSize().width()));
    ZX_DEBUG_ASSERT(frame->frame->coded_height ==
                    static_cast<uint32_t>(media_decoder_->GetPicSize().height()));
    if (!frame->in_use && !frame->in_internal_use) {
      frame->in_use = true;
      frame->in_internal_use = true;
      return frame;
    }
  }
  return nullptr;
}

zx_status_t H264MultiDecoder::SetupProtection() {
  return owner_->SetProtected(VideoDecoder::Owner::ProtectableHardwareUnit::kVdec, is_secure());
}
