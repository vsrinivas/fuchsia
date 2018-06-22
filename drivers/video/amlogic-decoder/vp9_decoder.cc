// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vp9_decoder.h"

#include "firmware_blob.h"
#include "macros.h"
#include "third_party/libvpx/vp9/common/vp9_loopfilter.h"

using HevcDecStatusReg = HevcAssistScratch0;
using HevcRpmBuffer = HevcAssistScratch1;
using HevcShortTermRps = HevcAssistScratch2;
using Vp9AdaptProbReg = HevcAssistScratch3;
using Vp9MmuMapBuffer = HevcAssistScratch4;
using HevcPpsBuffer = HevcAssistScratch5;
using HevcSaoUp = HevcAssistScratch6;
using HevcStreamSwapBuffer = HevcAssistScratch7;
using HevcStreamSwapBuffer2 = HevcAssistScratch8;
using Vp9ProbSwapBuffer = HevcAssistScratch9;
using Vp9CountSwapBuffer = HevcAssistScratchA;
using Vp9SegMapBuffer = HevcAssistScratchB;
using HevcScaleLut = HevcAssistScratchD;
using HevcLmemDumpAdr = HevcAssistScratchF;
using DecodeMode = HevcAssistScratchJ;
using HevcStreamSwapTest = HevcAssistScratchL;
using HevcWaitFlag = HevcAssistScratchE;
using NalSearchCtl = HevcAssistScratchI;
using DecodeStopPos = HevcAssistScratchK;
using HevcDecodeCount = HevcAssistScratchM;
using HevcDecodeSize = HevcAssistScratchN;

using DebugReg1 = HevcAssistScratchG;

// The hardware takes some uncompressed header information and stores it in this
// structure.
union Vp9Decoder::HardwareRenderParams {
  uint16_t data_words[0x80];
  struct {
    uint16_t profile;
    uint16_t show_existing_frame;
    uint16_t frame_to_show;  // If show_existing frame is 1.
    uint16_t frame_type;     // 0 is KEY_FRAME, 1 is INTER_FRAME
    uint16_t show_frame;
    uint16_t error_resilient_mode;
    uint16_t intra_only;
    uint16_t render_size_present;
    uint16_t reset_frame_context;
    uint16_t refresh_frame_flags;
    uint16_t width;
    uint16_t height;
    uint16_t render_width;
    uint16_t render_height;
    uint16_t ref_info;
    uint16_t same_frame_size;

    // These correspond with loop-filter information.
    uint16_t mode_ref_delta_enabled;
    uint16_t ref_deltas[4];
    uint16_t mode_deltas[2];
    uint16_t filter_level;
    uint16_t sharpness_level;
    uint16_t bit_depth;
    uint16_t segmentation_quant_info[8];
    uint16_t segmentation_enabled;
    uint16_t segmentation_abs_delta;
    uint16_t segmentation_loop_filter_info[8];
  };
};

void Vp9Decoder::BufferAllocator::Register(WorkingBuffer* buffer) {
  buffers_.push_back(buffer);
}

zx_status_t Vp9Decoder::BufferAllocator::AllocateBuffers(
    VideoDecoder::Owner* owner) {
  for (auto* buffer : buffers_) {
    zx_status_t status =
        io_buffer_init(&buffer->buffer(), owner->bti(), buffer->size(),
                       IO_BUFFER_CONTIG | IO_BUFFER_RW);
    if (status != ZX_OK) {
      DECODE_ERROR("VP9 working buffer allocation failed: %d\n", status);
      return status;
    }
    io_buffer_cache_flush_invalidate(&buffer->buffer(), 0, buffer->size());
  }
  return ZX_OK;
}

Vp9Decoder::WorkingBuffer::WorkingBuffer(BufferAllocator* allocator,
                                         size_t size)
    : size_(size) {
  allocator->Register(this);
}

Vp9Decoder::WorkingBuffer::~WorkingBuffer() { io_buffer_release(&buffer_); }

uint32_t Vp9Decoder::WorkingBuffer::addr32() {
  return truncate_to_32(io_buffer_phys(&buffer_));
}

Vp9Decoder::Vp9Decoder(Owner* owner) : owner_(owner) {}

Vp9Decoder::~Vp9Decoder() {
  owner_->core()->StopDecoding();
  owner_->core()->WaitForIdle();
}

void Vp9Decoder::UpdateLoopFilterThresholds() {
  for (uint32_t i = 0; i < MAX_LOOP_FILTER / 2; i++) {
    uint32_t threshold = 0;
    for (uint32_t j = 0; j < 2; j++) {
      uint32_t new_threshold =
          ((loop_filter_info_->lfthr[i * 2 + j].lim[0] & 0x3f) << 8) |
          (loop_filter_info_->lfthr[i * 2 + j].mblim[0] & 0xff);
      assert(16 * j < sizeof(threshold) * 8);
      threshold |= new_threshold << (16 * j);
    }

    HevcDblkCfg9::Get().FromValue(threshold).WriteTo(owner_->dosbus());
  }
}

void Vp9Decoder::InitLoopFilter() {
  vp9_loop_filter_init(loop_filter_info_.get(), loop_filter_.get());

  UpdateLoopFilterThresholds();
  if (owner_->device_type() == DeviceType::kG12A) {
    HevcDblkCfgB::Get()
        .FromValue(0x54 << 8)
        .set_vp9_mode(1)
        .set_compressed_write_enable(true)
        .set_uncompressed_write_enable(true)
        .WriteTo(owner_->dosbus());
  } else {
    HevcDblkCfgB::Get().FromValue(0x40400001).WriteTo(owner_->dosbus());
  }
}

void Vp9Decoder::UpdateLoopFilter(HardwareRenderParams* param) {
  loop_filter_->mode_ref_delta_enabled = param->mode_ref_delta_enabled;
  loop_filter_->sharpness_level = param->sharpness_level;
  for (uint32_t i = 0; i < fbl::count_of(param->ref_deltas); i++)
    loop_filter_->ref_deltas[i] = param->ref_deltas[i];
  for (uint32_t i = 0; i < fbl::count_of(param->mode_deltas); i++)
    loop_filter_->mode_deltas[i] = param->mode_deltas[i];

  segmentation_->enabled = param->segmentation_enabled;
  segmentation_->abs_delta = param->segmentation_abs_delta;
  for (uint32_t i = 0; i < MAX_SEGMENTS; i++) {
    segmentation_->feature_mask[i] =
        (param->segmentation_loop_filter_info[i] & 0x8000)
            ? (1 << SEG_LVL_ALT_LF)
            : 0;
    uint32_t abs_value = param->segmentation_loop_filter_info[i] & 0x3f;
    segmentation_->feature_data[i][SEG_LVL_ALT_LF] =
        (param->segmentation_loop_filter_info[i] & 0x100) ? -abs_value
                                                          : abs_value;
  }
  bool updated_sharpness;
  vp9_loop_filter_frame_init(loop_filter_.get(), loop_filter_info_.get(),
                             segmentation_.get(), param->filter_level,
                             &updated_sharpness);
  if (updated_sharpness)
    UpdateLoopFilterThresholds();
  for (uint32_t i = 0; i < MAX_SEGMENTS; i++) {
    for (uint32_t j = 0; j < MAX_MODE_LF_DELTAS; j++) {
      uint32_t level = 0;
      if (param->filter_level) {
        for (uint32_t k = 0; k < MAX_REF_FRAMES; ++k) {
          assert(k < sizeof(level));
          level |= (loop_filter_info_->lvl[i][k][j] & 0x3f) << (k * 8);
        }
      }
      HevcDblkCfgA::Get().FromValue(level).WriteTo(owner_->dosbus());
    }
  }
}

zx_status_t Vp9Decoder::Initialize() {
  uint8_t* firmware;
  uint32_t firmware_size;
  FirmwareBlob::FirmwareType firmware_type =
      (owner_->device_type() == DeviceType::kG12A)
          ? FirmwareBlob::FirmwareType::kVp9MmuG12a
          : FirmwareBlob::FirmwareType::kVp9Mmu;

  zx_status_t status = owner_->firmware_blob()->GetFirmwareData(
      firmware_type, &firmware, &firmware_size);
  if (status != ZX_OK)
    return status;

  status = owner_->core()->LoadFirmware(firmware, firmware_size);
  if (status != ZX_OK)
    return status;

  status = working_buffers_.AllocateBuffers(owner_);
  if (status != ZX_OK)
    return status;

  loop_filter_info_ = std::make_unique<loop_filter_info_n>();
  loop_filter_ = std::make_unique<loopfilter>();
  segmentation_ = std::make_unique<segmentation>();

  HevcRpmBuffer::Get()
      .FromValue(working_buffers_.rpm.addr32())
      .WriteTo(owner_->dosbus());
  HevcShortTermRps::Get()
      .FromValue(working_buffers_.short_term_rps.addr32())
      .WriteTo(owner_->dosbus());
  HevcPpsBuffer::Get()
      .FromValue(working_buffers_.picture_parameter_set.addr32())
      .WriteTo(owner_->dosbus());
  HevcStreamSwapBuffer::Get()
      .FromValue(working_buffers_.swap.addr32())
      .WriteTo(owner_->dosbus());
  HevcStreamSwapBuffer2::Get()
      .FromValue(working_buffers_.swap2.addr32())
      .WriteTo(owner_->dosbus());
  HevcLmemDumpAdr::Get()
      .FromValue(working_buffers_.local_memory_dump.addr32())
      .WriteTo(owner_->dosbus());
  HevcdIppLinebuffBase::Get()
      .FromValue(working_buffers_.ipp_line_buffer.addr32())
      .WriteTo(owner_->dosbus());
  HevcSaoUp::Get()
      .FromValue(working_buffers_.sao_up.addr32())
      .WriteTo(owner_->dosbus());
  HevcScaleLut::Get()
      .FromValue(working_buffers_.scale_lut.addr32())
      .WriteTo(owner_->dosbus());

  if (owner_->device_type() == DeviceType::kG12A) {
    HevcDblkCfgE::Get()
        .FromValue(working_buffers_.deblock_data2.addr32())
        .WriteTo(owner_->dosbus());
  }
  HevcDblkCfg4::Get()
      .FromValue(working_buffers_.deblock_parameters.addr32())
      .WriteTo(owner_->dosbus());

  HevcDblkCfg5::Get()
      .FromValue(working_buffers_.deblock_data.addr32())
      .WriteTo(owner_->dosbus());

  HevcdMppDecompCtl1::Get().FromValue(0).set_paged_mode(1).WriteTo(
      owner_->dosbus());
  HevcdMppDecompCtl2::Get().FromValue(0).WriteTo(owner_->dosbus());

  HevcSaoMmuVh0Addr::Get()
      .FromValue(working_buffers_.mmu_vbh.addr32())
      .WriteTo(owner_->dosbus());
  HevcSaoMmuVh1Addr::Get()
      .FromValue(working_buffers_.mmu_vbh.addr32() +
                 working_buffers_.mmu_vbh.size() / 2)
      .WriteTo(owner_->dosbus());
  HevcSaoCtrl5::Get()
      .ReadFrom(owner_->dosbus())
      .set_use_compressed_header(1)
      .WriteTo(owner_->dosbus());

  Vp9SegMapBuffer::Get()
      .FromValue(working_buffers_.segment_map.addr32())
      .WriteTo(owner_->dosbus());
  Vp9ProbSwapBuffer::Get()
      .FromValue(working_buffers_.probability_buffer.addr32())
      .WriteTo(owner_->dosbus());
  Vp9CountSwapBuffer::Get()
      .FromValue(working_buffers_.count_buffer.addr32())
      .WriteTo(owner_->dosbus());

  if (owner_->device_type() == DeviceType::kG12A) {
    HevcAssistMmuMapAddr::Get()
        .FromValue(working_buffers_.frame_map_mmu.addr32())
        .WriteTo(owner_->dosbus());
  } else {
    Vp9MmuMapBuffer::Get()
        .FromValue(working_buffers_.frame_map_mmu.addr32())
        .WriteTo(owner_->dosbus());
  }

  status = AllocateFrames();
  if (status != ZX_OK)
    return status;

  InitializeHardwarePictureList();
  InitializeParser();
  InitLoopFilter();

  HevcWaitFlag::Get().FromValue(1).WriteTo(owner_->dosbus());

  // The current firmware uses interrupt 0 to communicate.
  HevcAssistMbox0ClrReg::Get().FromValue(1).WriteTo(owner_->dosbus());
  HevcAssistMbox0Mask::Get().FromValue(1).WriteTo(owner_->dosbus());
  HevcPscaleCtrl::Get().FromValue(0).WriteTo(owner_->dosbus());
  DebugReg1::Get().FromValue(0).WriteTo(owner_->dosbus());
  NalSearchCtl::Get().FromValue(8).WriteTo(owner_->dosbus());

  DecodeStopPos::Get().FromValue(0).WriteTo(owner_->dosbus());

  owner_->core()->StartDecoding();
  return ZX_OK;
}

static uint32_t ComputeCompressedBodySize(uint32_t width, uint32_t height,
                                          bool is_10_bits) {
  uint32_t block_width = fbl::round_up(width, 64u) / 64;
  uint32_t block_height = fbl::round_up(height, 32u) / 32;
  uint32_t bytes_per_block = is_10_bits ? 4096 : 3200;
  return block_width * block_height * bytes_per_block;
}

static uint32_t ComputeCompressedHeaderSize(uint32_t width, uint32_t height,
                                            bool is_10_bits) {
  // Header blocks are twice the size of body blocks.
  uint32_t block_width = fbl::round_up(width, 128u) / 128;
  uint32_t block_height = fbl::round_up(height, 64u) / 64;
  constexpr uint32_t kBytesPerBlock = 32;
  return block_width * block_height * kBytesPerBlock;
}

void Vp9Decoder::ProcessCompletedFrames() {
  // On the first interrupt no frame will be completed.
  if (!current_frame_)
    return;

  if (notifier_)
    notifier_(current_frame_->frame.get());

  for (uint32_t i = 0; i < fbl::count_of(reference_frame_map_); i++) {
    if (current_frame_data_.refresh_frame_flags & (1 << i)) {
      if (reference_frame_map_[i]) {
        reference_frame_map_[i]->refcount--;
        assert(reference_frame_map_[i]->refcount >= 0);
      }
      reference_frame_map_[i] = current_frame_;
      current_frame_->refcount++;
    }
  }
  for (Frame*& frame : current_reference_frames_) {
    frame = nullptr;
  }
  if (last_frame_) {
    last_frame_->refcount--;
  }
  last_frame_ = current_frame_;
  current_frame_ = nullptr;
}

void Vp9Decoder::HandleInterrupt() {
  DLOG("Got VP9 interrupt\n");

  HevcAssistMbox0ClrReg::Get().FromValue(1).WriteTo(owner_->dosbus());

  uint32_t dec_status =
      HevcDecStatusReg::Get().ReadFrom(owner_->dosbus()).reg_value();
  uint32_t adapt_prob_status =
      Vp9AdaptProbReg::Get().ReadFrom(owner_->dosbus()).reg_value();

  DLOG("Decoder state: %x %x\n", dec_status, adapt_prob_status);

  ProcessCompletedFrames();

  enum {
    kProcessedHeader = 0xf0,
  };
  if (dec_status != kProcessedHeader) {
    DECODE_ERROR("Unexpected decode status %x\n", dec_status);
    return;
  };

  PrepareNewFrame();

  // PrepareNewFrame will tell the firmware to continue decoding if necessary.
}

void Vp9Decoder::ConfigureMcrcc() {
  // The MCRCC seems to be used with processing reference frames.
  HevcdMcrccCtl1::Get().FromValue(0).set_reset(true).WriteTo(owner_->dosbus());
  if (current_frame_data_.keyframe || current_frame_data_.intra_only) {
    HevcdMcrccCtl1::Get().FromValue(0).set_reset(false).WriteTo(
        owner_->dosbus());
    return;
  }
  // Signal an autoincrementing read of some canvas table.
  HevcdMppAncCanvasAccconfigAddr::Get().FromValue(0).set_bit1(1).WriteTo(
      owner_->dosbus());
  // First element is probably for last frame.
  uint32_t data_addr =
      HevcdMppAncCanvasDataAddr::Get().ReadFrom(owner_->dosbus()).reg_value();
  data_addr &= 0xffff;
  HevcdMcrccCtl2::Get()
      .FromValue(data_addr | (data_addr << 16))
      .WriteTo(owner_->dosbus());

  // Second element is probably for golden frame.
  data_addr =
      HevcdMppAncCanvasDataAddr::Get().ReadFrom(owner_->dosbus()).reg_value();
  data_addr &= 0xffff;
  HevcdMcrccCtl3::Get()
      .FromValue(data_addr | (data_addr << 16))
      .WriteTo(owner_->dosbus());
  // Set to progressive mode.
  HevcdMcrccCtl1::Get().FromValue(0xff0).WriteTo(owner_->dosbus());
}

void Vp9Decoder::ConfigureMotionPrediction() {
  // Intra frames and frames after intra frames can't use the previous
  // frame's mvs.
  if (current_frame_data_.keyframe || current_frame_data_.intra_only) {
    HevcMpredCtrl4::Get()
        .ReadFrom(owner_->dosbus())
        .set_use_prev_frame_mvs(false)
        .WriteTo(owner_->dosbus());
    return;
  }

  // Not sure what this value means.
  HevcMpredCtrl3::Get().FromValue(0x24122412).WriteTo(owner_->dosbus());
  HevcMpredAbvStartAddr::Get()
      .FromValue(working_buffers_.motion_prediction_above.addr32())
      .WriteTo(owner_->dosbus());

  bool last_frame_has_mv =
      !last_frame_data_.keyframe && !last_frame_data_.intra_only && last_frame_;
  HevcMpredCtrl4::Get()
      .ReadFrom(owner_->dosbus())
      .set_use_prev_frame_mvs(last_frame_has_mv)
      .WriteTo(owner_->dosbus());

  uint32_t mv_mpred_addr =
      truncate_to_32(io_buffer_phys(&current_frame_->mv_mpred_buffer));
  HevcMpredMvWrStartAddr::Get()
      .FromValue(mv_mpred_addr)
      .WriteTo(owner_->dosbus());
  HevcMpredMvWptr::Get().FromValue(mv_mpred_addr).WriteTo(owner_->dosbus());
  if (last_frame_) {
    uint32_t last_mv_mpred_addr =
        truncate_to_32(io_buffer_phys(&last_frame_->mv_mpred_buffer));
    HevcMpredMvRdStartAddr::Get()
        .FromValue(last_mv_mpred_addr)
        .WriteTo(owner_->dosbus());
    HevcMpredMvRptr::Get()
        .FromValue(last_mv_mpred_addr)
        .WriteTo(owner_->dosbus());

    uint32_t last_end_addr =
        last_mv_mpred_addr + io_buffer_size(&last_frame_->mv_mpred_buffer, 0);
    HevcMpredMvRdEndAddr::Get()
        .FromValue(last_end_addr)
        .WriteTo(owner_->dosbus());
  }
}

void Vp9Decoder::ConfigureFrameOutput(uint32_t width, uint32_t height) {
  // SAO stands for Sample Adaptive Offset, which is a type of filtering in
  // HEVC. Sao isn't used in VP9, but the hardware that handles it also handles
  // writing frames to memory.

  HevcSaoCtrl5::Get()
      .ReadFrom(owner_->dosbus())
      .set_mode_8_bits(true)
      .WriteTo(owner_->dosbus());

  HevcdMppDecompCtl1::Get().FromValue(0).set_paged_mode(1).WriteTo(
      owner_->dosbus());
  uint32_t compressed_body_size =
      ComputeCompressedBodySize(width, height, false);
  uint32_t compressed_header_size =
      ComputeCompressedHeaderSize(width, height, false);

  HevcdMppDecompCtl2::Get()
      .FromValue(compressed_body_size >> 5)
      .WriteTo(owner_->dosbus());
  HevcCmBodyLength::Get()
      .FromValue(compressed_body_size)
      .WriteTo(owner_->dosbus());
  // It's unclear if the header offset means anything with the MMU enabled, as
  // the header is stored separately.
  HevcCmHeaderOffset::Get()
      .FromValue(compressed_body_size)
      .WriteTo(owner_->dosbus());
  HevcCmHeaderLength::Get()
      .FromValue(compressed_header_size)
      .WriteTo(owner_->dosbus());
  HevcCmHeaderStartAddr::Get()
      .FromValue(
          truncate_to_32(io_buffer_phys(&current_frame_->compressed_header)))
      .WriteTo(owner_->dosbus());
  assert(compressed_header_size <=
         io_buffer_size(&current_frame_->compressed_header, 0));

  uint32_t frame_count =
      fbl::round_up(compressed_body_size, static_cast<uint32_t>(PAGE_SIZE)) /
      PAGE_SIZE;
  if (!io_buffer_is_valid(&current_frame_->compressed_data)) {
    zx_status_t status =
        io_buffer_init(&current_frame_->compressed_data, owner_->bti(),
                       PAGE_SIZE * frame_count, IO_BUFFER_RW);
    if (status != ZX_OK) {
      DECODE_ERROR("Couldn't allocate compressed frame data: %d\n", status);
      return;
    }

    status = io_buffer_physmap(&current_frame_->compressed_data);
    if (status != ZX_OK) {
      DECODE_ERROR("Couldn't map compressed frame data: %d\n", status);
      return;
    }
    io_buffer_cache_flush(&current_frame_->compressed_data, 0,
                          PAGE_SIZE * frame_count);
  }

  // Enough frames for the maximum possible size of compressed video have to be
  // allocated ahead of time. The hardware will read them from
  // frame_map_mmu.buffer as needed.
  //
  // TODO(MTWN-148): Return unused frames could be returned to a pool and use
  // them for decoding a different frame.
  {
    uint32_t* mmu_data = static_cast<uint32_t*>(
        io_buffer_virt(&working_buffers_.frame_map_mmu.buffer()));
    for (uint32_t i = 0; i < frame_count; i++) {
      mmu_data[i] = current_frame_->compressed_data.phys_list[i] >> 12;
    }
    io_buffer_cache_flush(&working_buffers_.frame_map_mmu.buffer(), 0,
                          frame_count * 4);
  }

  uint32_t buffer_address =
      truncate_to_32(io_buffer_phys(&current_frame_->frame->buffer));

  HevcSaoYStartAddr::Get().FromValue(buffer_address).WriteTo(owner_->dosbus());
  HevcSaoYWptr::Get().FromValue(buffer_address).WriteTo(owner_->dosbus());
  HevcSaoCStartAddr::Get()
      .FromValue(buffer_address + current_frame_->frame->uv_plane_offset)
      .WriteTo(owner_->dosbus());
  HevcSaoCWptr::Get()
      .FromValue(buffer_address + current_frame_->frame->uv_plane_offset)
      .WriteTo(owner_->dosbus());

  // There's no way to specify a non-tightly-packed stride.
  HevcSaoYLength::Get().FromValue(width * height).WriteTo(owner_->dosbus());
  HevcSaoCLength::Get().FromValue(width * height / 2).WriteTo(owner_->dosbus());
  // Compressed data is used as a reference for future frames, and uncompressed
  // data is output to consumers. Uncompressed data writes could be disabled in
  // the future if the consumer (e.g. the display) supported reading the
  // compressed data.
  {
    auto temp = HevcSaoCtrl1::Get().ReadFrom(owner_->dosbus());
    temp.set_mem_map_mode(HevcSaoCtrl1::kMemMapModeLinear)
        .set_endianness(HevcSaoCtrl1::kBigEndian64);
    if (owner_->device_type() == DeviceType::kG12A) {
      HevcDblkCfgB::Get()
          .ReadFrom(owner_->dosbus())
          .set_compressed_write_enable(true)
          .set_uncompressed_write_enable(true)
          .WriteTo(owner_->dosbus());
    } else {
      temp.set_double_write_disable(false).set_compressed_write_disable(false);
    }
    temp.WriteTo(owner_->dosbus());
  }

  {
    auto temp = HevcSaoCtrl5::Get().ReadFrom(owner_->dosbus());
    temp.set_reg_value(~(0xff << 16) & temp.reg_value());

    temp.WriteTo(owner_->dosbus());
  }
  HevcdIppAxiifConfig::Get()
      .ReadFrom(owner_->dosbus())
      .set_mem_map_mode(HevcdIppAxiifConfig::kMemMapModeLinear)
      .set_double_write_endian(HevcdIppAxiifConfig::kBigEndian64)
      .WriteTo(owner_->dosbus());
}

void Vp9Decoder::PrepareNewFrame() {
  HardwareRenderParams params;
  io_buffer_cache_flush_invalidate(&working_buffers_.rpm.buffer(), 0,
                                   sizeof(HardwareRenderParams));
  uint16_t* input_params =
      static_cast<uint16_t*>(io_buffer_virt(&working_buffers_.rpm.buffer()));

  // Convert from middle-endian.
  for (uint32_t i = 0; i < fbl::count_of(params.data_words); i += 4) {
    for (uint32_t j = 0; j < 4; j++) {
      params.data_words[i + j] = input_params[i + (3 - j)];
    }
  }

  last_frame_data_ = current_frame_data_;
  current_frame_data_.keyframe = params.frame_type == 0;
  current_frame_data_.intra_only = params.intra_only;
  current_frame_data_.refresh_frame_flags = params.refresh_frame_flags;
  if (current_frame_data_.keyframe) {
    current_frame_data_.refresh_frame_flags =
        (1 << fbl::count_of(reference_frame_map_)) - 1;
  }

  // TODO(MTWN-149): Wait for old frames to be returned before continuing to
  // decode.
  if (!FindNewFrameBuffer(&params))
    return;

  SetRefFrames(&params);

  uint32_t width = params.width;
  uint32_t height = params.height;
  HevcParserPictureSize::Get()
      .FromValue((height << 16) | width)
      .WriteTo(owner_->dosbus());

  ConfigureReferenceFrameHardware();
  ConfigureMotionPrediction();
  ConfigureMcrcc();

  ConfigureFrameOutput(width, height);

  UpdateLoopFilter(&params);

  enum {
    kDecodeSlice = 5,
  };

  HevcDecStatusReg::Get().FromValue(kDecodeSlice).WriteTo(owner_->dosbus());
}

void Vp9Decoder::SetFrameReadyNotifier(FrameReadyNotifier notifier) {
  notifier_ = notifier;
}

Vp9Decoder::Frame::~Frame() {
  io_buffer_release(&compressed_header);
  io_buffer_release(&compressed_data);
  io_buffer_release(&mv_mpred_buffer);
}

bool Vp9Decoder::FindNewFrameBuffer(HardwareRenderParams* params) {
  assert(!current_frame_);
  Frame* new_frame = nullptr;
  for (uint32_t i = 0; i < frames_.size(); i++) {
    if (frames_[i]->refcount == 0) {
      new_frame = frames_[i].get();
      break;
    }
  }
  if (!new_frame) {
    DECODE_ERROR("Couldn't allocate framebuffer - all in use\n");
    return false;
  }

  if (!new_frame->frame || (new_frame->frame->width != params->width) ||
      (new_frame->frame->height != params->height)) {
    auto video_frame = std::make_unique<VideoFrame>();
    uint32_t width = params->width;
    uint32_t height = params->height;
    zx_status_t status =
        io_buffer_init(&video_frame->buffer, owner_->bti(),
                       width * height * 3 / 2, IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
      DECODE_ERROR("Failed to make video_frame: %d\n", status);
      return false;
    }

    video_frame->uv_plane_offset = width * height;
    video_frame->stride = width;
    video_frame->width = width;
    video_frame->height = height;
    new_frame->frame = std::move(video_frame);

    // The largest coding unit is assumed to be 64x32.
    constexpr uint32_t kLcuMvBytes = 0x240;
    constexpr uint32_t kLcuCount = 4096 * 2048 / (64 * 32);
    status = io_buffer_init_aligned(&new_frame->mv_mpred_buffer, owner_->bti(),
                                    kLcuCount * kLcuMvBytes, 16,
                                    IO_BUFFER_CONTIG | IO_BUFFER_RW);
    if (status != ZX_OK) {
      DECODE_ERROR("Alloc buffer error: %d\n", status);
      return false;
    }
    io_buffer_cache_flush_invalidate(&new_frame->mv_mpred_buffer, 0,
                                     kLcuCount * kLcuMvBytes);
  }

  current_frame_ = new_frame;
  current_frame_->refcount++;
  current_frame_->decoded_index = decoded_frame_count_++;
  return true;
}

void Vp9Decoder::SetRefFrames(HardwareRenderParams* params) {
  uint32_t reference_frame_count = fbl::count_of(current_reference_frames_);
  for (uint32_t i = 0; i < reference_frame_count; i++) {
    uint32_t ref =
        (params->ref_info >> (((reference_frame_count - 1 - i) * 4) + 1)) & 0x7;
    assert(ref < fbl::count_of(reference_frame_map_));
    current_reference_frames_[i] = reference_frame_map_[ref];
  }
}

void Vp9Decoder::ConfigureReferenceFrameHardware() {
  // Do an autoincrementing write to one canvas table.
  HevcdMppAncCanvasAccconfigAddr::Get().FromValue(0).set_bit0(1).WriteTo(
      owner_->dosbus());
  for (Frame* frame : current_reference_frames_) {
    if (!frame)
      continue;
    HevcdMppAncCanvasDataAddr::Get()
        .FromValue((frame->index << 16) | (frame->index << 8) | (frame->index))
        .WriteTo(owner_->dosbus());
  }

  // Do an autoincrementing write to a different canvas table.
  HevcdMppAncCanvasAccconfigAddr::Get()
      .FromValue(0)
      .set_field15_8(16)
      .set_bit0(1)
      .WriteTo(owner_->dosbus());

  for (Frame* frame : current_reference_frames_) {
    if (!frame)
      continue;
    HevcdMppAncCanvasDataAddr::Get()
        .FromValue((frame->index << 16) | (frame->index << 8) | (frame->index))
        .WriteTo(owner_->dosbus());
  }

  // Do an autoincrementing write to the reference info table.
  Vp9dMppRefinfoTblAccconfig::Get().FromValue(0).set_bit2(1).WriteTo(
      owner_->dosbus());
  uint32_t scale_mask = 0;
  for (uint32_t i = 0; i < fbl::count_of(current_reference_frames_); i++) {
    Frame* frame = current_reference_frames_[i];
    if (!frame)
      continue;
    Vp9dMppRefinfoData::Get()
        .FromValue(frame->frame->width)
        .WriteTo(owner_->dosbus());
    Vp9dMppRefinfoData::Get()
        .FromValue(frame->frame->height)
        .WriteTo(owner_->dosbus());

    if (current_frame_->frame->width != frame->frame->width ||
        current_frame_->frame->height != frame->frame->height) {
      scale_mask |= 1 << i;
    }
    Vp9dMppRefinfoData::Get()
        .FromValue((frame->frame->width << 14) / current_frame_->frame->width)
        .WriteTo(owner_->dosbus());
    Vp9dMppRefinfoData::Get()
        .FromValue((frame->frame->height << 14) / current_frame_->frame->height)
        .WriteTo(owner_->dosbus());
    // Copmpressed body size. 0 If dynamically allocated
    Vp9dMppRefinfoData::Get().FromValue(0).WriteTo(owner_->dosbus());
  }

  Vp9dMppRefScaleEnable::Get().FromValue(scale_mask).WriteTo(owner_->dosbus());
}

zx_status_t Vp9Decoder::AllocateFrames() {
  // The VP9 format need 8 reference pictures, plus keep some extra ones that
  // are available for use later in the pipeline.
  for (uint32_t i = 0; i < 16; i++) {
    auto frame = std::make_unique<Frame>();
    constexpr uint32_t kCompressedHeaderSize = 0x48000;
    zx_status_t status = io_buffer_init_aligned(
        &frame->compressed_header, owner_->bti(), kCompressedHeaderSize, 16,
        IO_BUFFER_CONTIG | IO_BUFFER_RW);
    if (status != ZX_OK) {
      DECODE_ERROR("Alloc buffer error: %d\n", status);
      return status;
    }
    io_buffer_cache_flush_invalidate(&frame->compressed_header, 0,
                                     kCompressedHeaderSize);
    frame->index = i;
    frames_.push_back(std::move(frame));
  }

  return ZX_OK;
}

void Vp9Decoder::InitializeHardwarePictureList() {
  // Signal autoincrementing writes to table.
  HevcdMppAnc2AxiTblConfAddr::Get()
      .FromValue(0)
      .set_bit1(1)
      .set_bit2(1)
      .WriteTo(owner_->dosbus());

  // This table maps "canvas" indices to the compressed headers of reference
  // pictures.
  for (auto& frame : frames_) {
    HevcdMppAnc2AxiTblData::Get()
        .FromValue(
            truncate_to_32(io_buffer_phys(&frame->compressed_header) >> 5))
        .WriteTo(owner_->dosbus());
  }

  HevcdMppAnc2AxiTblConfAddr::Get().FromValue(1).WriteTo(owner_->dosbus());

  // Set all reference picture canvas indices to 0 - do an autoincrementing
  // write.
  HevcdMppAncCanvasAccconfigAddr::Get().FromValue(0).set_bit0(1).WriteTo(
      owner_->dosbus());
  for (uint32_t i = 0; i < 32; ++i) {
    HevcdMppAncCanvasDataAddr::Get().FromValue(0).WriteTo(owner_->dosbus());
  }
}

void Vp9Decoder::InitializeParser() {
  HevcParserIntControl::Get()
      .ReadFrom(owner_->dosbus())
      .set_fifo_ctl(3)
      .set_stream_buffer_empty_amrisc_enable(1)
      .set_stream_fifo_empty_amrisc_enable(1)
      .set_dec_done_int_cpu_enable(1)
      .set_startcode_found_int_cpu_enable(1)
      .set_parser_int_enable(1)
      .WriteTo(owner_->dosbus());
  HevcShiftStatus::Get()
      .ReadFrom(owner_->dosbus())
      .set_emulation_check(0)
      .set_startcode_check(1)
      .WriteTo(owner_->dosbus());
  HevcShiftControl::Get()
      .ReadFrom(owner_->dosbus())
      .set_start_code_protect(0)
      .set_length_zero_startcode(1)
      .set_length_valid_startcode(1)
      .set_sft_valid_wr_position(3)
      .set_emulate_code_length_minus1(2)
      .set_start_code_length_minus1(3)
      .set_stream_shift_enable(1)
      .WriteTo(owner_->dosbus());
  HevcCabacControl::Get().FromValue(0).set_enable(true).WriteTo(
      owner_->dosbus());
  HevcParserCoreControl::Get().FromValue(0).set_clock_enable(true).WriteTo(
      owner_->dosbus());
  HevcDecStatusReg::Get().FromValue(0).WriteTo(owner_->dosbus());

  HevcIqitScalelutWrAddr::Get().FromValue(0).WriteTo(owner_->dosbus());
  for (uint32_t i = 0; i < 1024; i++) {
    HevcIqitScalelutData::Get().FromValue(0).WriteTo(owner_->dosbus());
  }

  HevcStreamSwapTest::Get().FromValue(0).WriteTo(owner_->dosbus());
  enum DecodeModes {
    kDecodeModeSingle =
        (0x80 << 24),  // One decoder, instead of multiple at a time.
  };
  DecodeMode::Get().FromValue(kDecodeModeSingle).WriteTo(owner_->dosbus());
  HevcDecodeSize::Get().FromValue(0).WriteTo(owner_->dosbus());
  HevcDecodeCount::Get().FromValue(0).WriteTo(owner_->dosbus());

  HevcParserCmdWrite::Get().FromValue(1 << 16).WriteTo(owner_->dosbus());

  constexpr uint32_t parser_cmds[] = {
      0x0401, 0x8401, 0x0800, 0x0402, 0x9002, 0x1423, 0x8CC3, 0x1423,
      0x8804, 0x9825, 0x0800, 0x04FE, 0x8406, 0x8411, 0x1800, 0x8408,
      0x8409, 0x8C2A, 0x9C2B, 0x1C00, 0x840F, 0x8407, 0x8000, 0x8408,
      0x2000, 0xA800, 0x8410, 0x04DE, 0x840C, 0x840D, 0xAC00, 0xA000,
      0x08C0, 0x08E0, 0xA40E, 0xFC00, 0x7C00};

  for (uint32_t cmd : parser_cmds) {
    HevcParserCmdWrite::Get().FromValue(cmd).WriteTo(owner_->dosbus());
  }
  HevcParserCmdSkip0::Get().FromValue(0x0000090b).WriteTo(owner_->dosbus());
  HevcParserCmdSkip1::Get().FromValue(0x1b14140f).WriteTo(owner_->dosbus());
  HevcParserCmdSkip2::Get().FromValue(0x001b1910).WriteTo(owner_->dosbus());

  HevcParserIfControl::Get()
      .FromValue(0)
      .set_parser_sao_if_enable(true)
      .set_parser_mpred_if_enable(true)
      .set_parser_scaler_if_enable(true)
      .WriteTo(owner_->dosbus());
  HevcdIppTopCntl::Get().FromValue(0).set_reset_ipp_and_mpp(true).WriteTo(
      owner_->dosbus());
  HevcdIppTopCntl::Get().FromValue(0).set_enable_ipp(true).WriteTo(
      owner_->dosbus());

  if (owner_->device_type() == DeviceType::kG12A) {
    HevcStreamFifoCtl::Get()
        .ReadFrom(owner_->dosbus())
        .set_stream_fifo_hole(true)
        .WriteTo(owner_->dosbus());
  }

  // The input format is <32-bit big-endian length><32-bit big-endian length ^
  // 0xffffffff><00><00><00><01>AMLV, which must be inserted by software ahead
  // of time.
  HevcShiftStartCode::Get().FromValue(0x00000001).WriteTo(owner_->dosbus());
  // Shouldn't matter, since the emulation check is disabled.
  HevcShiftEmulateCode::Get().FromValue(0x00003000).WriteTo(owner_->dosbus());
}
