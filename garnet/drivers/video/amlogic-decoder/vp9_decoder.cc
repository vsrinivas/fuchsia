// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vp9_decoder.h"

#include <lib/media/codec_impl/codec_buffer.h>
#include <lib/media/codec_impl/codec_packet.h>

#include "firmware_blob.h"
#include "macros.h"
#include "memory_barriers.h"
#include "pts_manager.h"
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

// How much padding to put after buffers to validate their size  (in terms of
// how much data the HW/firmware actually writes to them). If 0, validation is
// skipped.
constexpr uint32_t kBufferOverrunPaddingBytes = 0;

void Vp9Decoder::BufferAllocator::Register(WorkingBuffer* buffer) {
  buffers_.push_back(buffer);
}

zx_status_t Vp9Decoder::BufferAllocator::AllocateBuffers(
    VideoDecoder::Owner* owner) {
  for (auto* buffer : buffers_) {
    zx_status_t status = owner->AllocateIoBuffer(
        &buffer->buffer(), buffer->size() + kBufferOverrunPaddingBytes, 0,
        IO_BUFFER_CONTIG | IO_BUFFER_RW);
    if (status != ZX_OK) {
      DECODE_ERROR("VP9 working buffer allocation failed: %d\n", status);
      return status;
    }
    if (kBufferOverrunPaddingBytes) {
      uint32_t real_buffer_size = io_buffer_size(&buffer->buffer(), 0);
      for (uint32_t i = buffer->size(); i < real_buffer_size; i++) {
        uint8_t* data =
            static_cast<uint8_t*>(io_buffer_virt(&buffer->buffer()));
        data[i] = i & 0xff;
      }
    }
    io_buffer_cache_flush_invalidate(
        &buffer->buffer(), 0, buffer->size() + kBufferOverrunPaddingBytes);
  }
  return ZX_OK;
}

// Check that the padding after every buffer hasn't been modified by hardware.
// This helps validate that buffers are large enough to store all data the
// hardware puts in them.
void Vp9Decoder::BufferAllocator::CheckBuffers() {
  if (kBufferOverrunPaddingBytes) {
    for (uint32_t buf_number = 0; buf_number < buffers_.size(); buf_number++) {
      auto* buffer = buffers_[buf_number];
      if (!io_buffer_is_valid(&buffer->buffer()))
        continue;
      uint32_t offset = buffer->size();
      uint8_t* data = static_cast<uint8_t*>(io_buffer_virt(&buffer->buffer()));
      uint32_t buffer_size = io_buffer_size(&buffer->buffer(), 0);
      io_buffer_cache_flush_invalidate(&buffer->buffer(), offset,
                                       buffer_size - offset);
      for (uint32_t i = offset; i < buffer_size; ++i) {
        if (data[i] != (i & 0xff)) {
          DECODE_ERROR("Data mismatch: %d != %d in buffer %d position %d\n",
                       data[i], (i & 0xff), buf_number, i);
        }
        ZX_DEBUG_ASSERT(data[i] == (i & 0xff));
      }
      io_buffer_cache_flush_invalidate(&buffer->buffer(), offset,
                                       buffer_size - offset);
    }
  }
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

Vp9Decoder::Vp9Decoder(Owner* owner, InputType input_type)
    : owner_(owner), input_type_(input_type) {
  InitializeLoopFilterData();
}

Vp9Decoder::~Vp9Decoder() {
  if (owner_->IsDecoderCurrent(this)) {
    owner_->core()->StopDecoding();
    owner_->core()->WaitForIdle();
  }

  BarrierBeforeRelease();  // For all working buffers
  working_buffers_.CheckBuffers();
}

void Vp9Decoder::UpdateLoopFilterThresholds() {
  for (uint32_t i = 0; i <= MAX_LOOP_FILTER / 2; i++) {
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

void Vp9Decoder::InitializeLoopFilterData() {
  loop_filter_info_ = std::make_unique<loop_filter_info_n>();
  loop_filter_ = std::make_unique<loopfilter>();
  segmentation_ = std::make_unique<segmentation>();

  vp9_loop_filter_init(loop_filter_info_.get(), loop_filter_.get());
}

void Vp9Decoder::InitLoopFilter() {
  UpdateLoopFilterThresholds();
  if (IsDeviceAtLeast(owner_->device_type(), DeviceType::kG12A)) {
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
  zx_status_t status = InitializeBuffers();
  if (status != ZX_OK)
    return status;
  return InitializeHardware();
}

zx_status_t Vp9Decoder::InitializeBuffers() {
  zx_status_t status = working_buffers_.AllocateBuffers(owner_);
  if (status != ZX_OK)
    return status;
  status = AllocateFrames();
  BarrierAfterFlush();  // For all frames and working buffers.
  return status;
}

zx_status_t Vp9Decoder::InitializeHardware() {
  ZX_DEBUG_ASSERT(state_ == DecoderState::kSwappedOut);
  assert(owner_->IsDecoderCurrent(this));
  working_buffers_.CheckBuffers();
  zx_status_t status = owner_->SetProtected(
      VideoDecoder::Owner::ProtectableHardwareUnit::kHevc, false);
  if (status != ZX_OK)
    return status;
  uint8_t* firmware;
  uint32_t firmware_size;
  FirmwareBlob::FirmwareType firmware_type =
      IsDeviceAtLeast(owner_->device_type(), DeviceType::kG12A)
          ? FirmwareBlob::FirmwareType::kVp9MmuG12a
          : FirmwareBlob::FirmwareType::kVp9Mmu;

  status = owner_->firmware_blob()->GetFirmwareData(firmware_type, &firmware,
                                                    &firmware_size);
  if (status != ZX_OK)
    return status;

  status = owner_->core()->LoadFirmware(firmware, firmware_size);
  if (status != ZX_OK)
    return status;

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

  if (IsDeviceAtLeast(owner_->device_type(), DeviceType::kG12A)) {
    HevcDblkCfgE::Get()
        .FromValue(working_buffers_.deblock_parameters2.addr32())
        .WriteTo(owner_->dosbus());
  }

  // The linux driver doesn't write to this register on G12A, but that seems to
  // cause the hardware to write some data to physical address 0 and corrupt
  // memory.
  HevcDblkCfg4::Get()
      .FromValue(working_buffers_.deblock_parameters.addr32())
      .WriteTo(owner_->dosbus());

  // The firmware expects the deblocking data to always follow the parameters.
  HevcDblkCfg5::Get()
      .FromValue(working_buffers_.deblock_parameters.addr32() +
                 WorkingBuffers::kDeblockParametersSize)
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

  if (IsDeviceAtLeast(owner_->device_type(), DeviceType::kG12A)) {
    HevcAssistMmuMapAddr::Get()
        .FromValue(working_buffers_.frame_map_mmu.addr32())
        .WriteTo(owner_->dosbus());
  } else {
    Vp9MmuMapBuffer::Get()
        .FromValue(working_buffers_.frame_map_mmu.addr32())
        .WriteTo(owner_->dosbus());
  }

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

  // In the multi-stream case, don't start yet to give the caller the chance
  // to restore the input state.
  if (input_type_ == InputType::kSingleStream) {
    state_ = DecoderState::kRunning;
    owner_->core()->StartDecoding();
  } else {
    state_ = DecoderState::kInitialWaitingForInput;
  }
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

  if (current_frame_data_.show_frame && notifier_) {
    current_frame_->frame->has_pts = current_frame_data_.has_pts;
    current_frame_->frame->pts = current_frame_data_.pts;
    current_frame_->refcount++;
    notifier_(current_frame_->frame);
  }

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

  cached_mpred_buffer_ = std::move(last_mpred_buffer_);
  last_mpred_buffer_ = std::move(current_mpred_buffer_);
}

void Vp9Decoder::InitializedFrames(std::vector<CodecFrame> frames,
                                   uint32_t width, uint32_t height,
                                   uint32_t stride) {
  ZX_DEBUG_ASSERT(state_ == DecoderState::kPausedAtHeader);
  uint32_t frame_vmo_bytes = height * stride + height * stride / 2;
  for (uint32_t i = 0; i < frames_.size(); i++) {
    auto video_frame = std::make_shared<VideoFrame>();
    video_frame->width = width;
    video_frame->height = height;
    video_frame->stride = stride;
    video_frame->uv_plane_offset = video_frame->stride * video_frame->height;
    video_frame->index = i;

    video_frame->codec_buffer = frames[i].codec_buffer_ptr;
    if (frames[i].codec_buffer_ptr) {
      frames[i].codec_buffer_ptr->SetVideoFrame(video_frame);
    }

    assert(video_frame->height % 2 == 0);
    assert(frames[i].codec_buffer_spec.has_data());
    assert(frames[i].codec_buffer_spec.data().is_vmo());
    assert(frames[i].codec_buffer_spec.data().vmo().has_vmo_handle());
    zx_status_t status = io_buffer_init_vmo(
        &video_frame->buffer, owner_->bti(),
        frames[i].codec_buffer_spec.data().vmo().vmo_handle().get(), 0,
        IO_BUFFER_RW);
    if (status != ZX_OK) {
      DECODE_ERROR("Failed to io_buffer_init_vmo() for frame - status: %d\n",
                   status);
      return;
    }
    size_t vmo_size = io_buffer_size(&video_frame->buffer, 0);
    if (vmo_size < frame_vmo_bytes) {
      DECODE_ERROR("Insufficient frame vmo bytes: %ld < %d\n", vmo_size,
                   frame_vmo_bytes);
      return;
    }
    status = io_buffer_physmap(&video_frame->buffer);
    if (status != ZX_OK) {
      DECODE_ERROR("Failed to io_buffer_physmap - status: %d\n", status);
      return;
    }

    for (uint32_t i = 1; i < vmo_size / PAGE_SIZE; i++) {
      if (video_frame->buffer.phys_list[i - 1] + PAGE_SIZE !=
          video_frame->buffer.phys_list[i]) {
        DECODE_ERROR("VMO isn't contiguous\n");
        return;
      }
    }

    io_buffer_cache_flush(&video_frame->buffer, 0,
                          io_buffer_size(&video_frame->buffer, 0));
    frames_[i]->frame = std::move(video_frame);
  }

  BarrierAfterFlush();

  ZX_DEBUG_ASSERT(waiting_for_empty_frames_);
  waiting_for_empty_frames_ = false;
  // Also updates state_.
  PrepareNewFrame();
}

void Vp9Decoder::ReturnFrame(std::shared_ptr<VideoFrame> frame) {
  assert(frame->index < frames_.size());
  auto& ref_frame = frames_[frame->index];
  // Frame must still be valid if the refcount is > 0.
  assert(ref_frame->frame == frame);
  ref_frame->refcount--;
  assert(ref_frame->refcount >= 0);

  // If either of these bools is true, we know the decoder isn't running.  It's
  // fine that we don't check here that there's a frame with refcount 0 or check
  // here that the output is ready, because PrepareNewFrame() will re-check
  // both those things, and set the appropriate waiting bool back to true if we
  // still need to wait.
  if (waiting_for_output_ready_ || waiting_for_empty_frames_) {
    waiting_for_output_ready_ = false;
    waiting_for_empty_frames_ = false;
    PrepareNewFrame();
  }
}
enum Vp9Command {
  // Sent from the host to the device after a header has been decoded to say
  // that the compressed frame body should be decoded.
  kVp9CommandDecodeSlice = 5,

  // Sent from the device to the host to say that a frame has finished decoding.
  // This is only sent in multi-stream mode.
  kVp9CommandDecodingDataDone = 0xa,

  // Sent from the device to the host to say that all of the input data (from
  // HevcDecodeSize) has been processed. Only sent in multi-stream mode.
  kVp9CommandNalDecodeDone = 0xe,

  // Sent from the device if it's attempted to read HevcDecodeSize bytes, but
  // couldn't because there wasn't enough input data. This can happen if the
  // ringbuffer is out of data or if there wasn't enough padding to flush enough
  // data through the HEVC parser fifo.
  kVp9InputBufferEmpty = 0x20,

  // Sent from the device to the host to say that a VP9 header has been
  // decoded and the parameter buffer has data. In single-stream mode this also
  // signals that the previous frame finished decoding.
  kProcessedHeader = 0xf0,

  // Sent from the host to the device to say that the last interrupt has been
  // processed.
  kVp9ActionDone = 0xff,
};

void Vp9Decoder::UpdateDecodeSize(uint32_t size) {
  ZX_DEBUG_ASSERT(state_ == DecoderState::kStoppedWaitingForInput ||
                  state_ == DecoderState::kInitialWaitingForInput);
  uint32_t old_decode_count =
      HevcDecodeCount::Get().ReadFrom(owner_->dosbus()).reg_value();
  if (old_decode_count != frame_done_count_) {
    HevcDecodeSize::Get().FromValue(0).WriteTo(owner_->dosbus());
    HevcDecodeCount::Get()
        .FromValue(frame_done_count_)
        .WriteTo(owner_->dosbus());
  }
  HevcDecodeSize::Get()
      .FromValue(HevcDecodeSize::Get().ReadFrom(owner_->dosbus()).reg_value() +
                 size)
      .WriteTo(owner_->dosbus());
  if (state_ == DecoderState::kStoppedWaitingForInput) {
    HevcDecStatusReg::Get().FromValue(kVp9ActionDone).WriteTo(owner_->dosbus());
  }
  owner_->core()->StartDecoding();
  state_ = DecoderState::kRunning;
}

void Vp9Decoder::HandleInterrupt() {
  DLOG("%p Got VP9 interrupt\n", this);
  ZX_DEBUG_ASSERT(state_ == DecoderState::kRunning);

  HevcAssistMbox0ClrReg::Get().FromValue(1).WriteTo(owner_->dosbus());

  uint32_t dec_status =
      HevcDecStatusReg::Get().ReadFrom(owner_->dosbus()).reg_value();
  uint32_t adapt_prob_status =
      Vp9AdaptProbReg::Get().ReadFrom(owner_->dosbus()).reg_value();

  DLOG("Decoder state: %x %x\n", dec_status, adapt_prob_status);

  if (dec_status == kVp9InputBufferEmpty) {
    // TODO: We'll want to use this to continue filling input data of
    // particularly large input frames, if we can get this to work. Currently
    // attempting to restart decoding after this in frame-based decoding mode
    // causes old data to be skipped.
    DECODE_ERROR("Input buffer empty, insufficient padding?\n");
    return;
  }
  if (dec_status == kVp9CommandNalDecodeDone) {
    owner_->core()->StopDecoding();
    state_ = DecoderState::kStoppedWaitingForInput;
    frame_data_provider_->ReadMoreInputData(this);
    return;
  }
  ProcessCompletedFrames();

  if (dec_status == kVp9CommandDecodingDataDone) {
    state_ = DecoderState::kFrameJustProduced;
    frame_done_count_++;
    frame_data_provider_->FrameWasOutput();
    if (state_ != DecoderState::kSwappedOut) {
      // TODO: Avoid running the decoder if there's no input data or output
      // buffers available. Once it starts running we don't let it swap out, so
      // one decoder could hang indefinitely in this case without being swapped
      // out. This can happen if the player's paused or if the client hangs.
      state_ = DecoderState::kRunning;
      HevcDecStatusReg::Get()
          .FromValue(kVp9ActionDone)
          .WriteTo(owner_->dosbus());
    }
    return;
  }
  if (dec_status != kProcessedHeader) {
    DECODE_ERROR("Unexpected decode status %x\n", dec_status);
    return;
  };

  state_ = DecoderState::kPausedAtHeader;

  PrepareNewFrame();
  DLOG("Done handling VP9 interrupt\n");

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

Vp9Decoder::MpredBuffer::~MpredBuffer() { io_buffer_release(&mv_mpred_buffer); }

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
      last_frame_ && !last_frame_data_.keyframe &&
      !last_frame_data_.intra_only &&
      current_frame_->frame->width == last_frame_->frame->width &&
      current_frame_->frame->height == last_frame_->frame->height &&
      !current_frame_data_.error_resilient_mode && last_frame_data_.show_frame;
  HevcMpredCtrl4::Get()
      .ReadFrom(owner_->dosbus())
      .set_use_prev_frame_mvs(last_frame_has_mv)
      .WriteTo(owner_->dosbus());

  uint32_t mv_mpred_addr =
      truncate_to_32(io_buffer_phys(&current_mpred_buffer_->mv_mpred_buffer));
  HevcMpredMvWrStartAddr::Get()
      .FromValue(mv_mpred_addr)
      .WriteTo(owner_->dosbus());
  HevcMpredMvWptr::Get().FromValue(mv_mpred_addr).WriteTo(owner_->dosbus());
  if (last_mpred_buffer_) {
    uint32_t last_mv_mpred_addr =
        truncate_to_32(io_buffer_phys(&last_mpred_buffer_->mv_mpred_buffer));
    HevcMpredMvRdStartAddr::Get()
        .FromValue(last_mv_mpred_addr)
        .WriteTo(owner_->dosbus());
    HevcMpredMvRptr::Get()
        .FromValue(last_mv_mpred_addr)
        .WriteTo(owner_->dosbus());

    uint32_t last_end_addr =
        last_mv_mpred_addr +
        io_buffer_size(&last_mpred_buffer_->mv_mpred_buffer, 0);
    HevcMpredMvRdEndAddr::Get()
        .FromValue(last_end_addr)
        .WriteTo(owner_->dosbus());
  }
}

void Vp9Decoder::ConfigureFrameOutput(uint32_t width, uint32_t height,
                                      bool bit_depth_8) {
  // SAO stands for Sample Adaptive Offset, which is a type of filtering in
  // HEVC. Sao isn't used in VP9, but the hardware that handles it also handles
  // writing frames to memory.

  HevcSaoCtrl5::Get()
      .ReadFrom(owner_->dosbus())
      .set_mode_8_bits(bit_depth_8)
      .WriteTo(owner_->dosbus());

  HevcdMppDecompCtl1::Get().FromValue(0).set_paged_mode(1).WriteTo(
      owner_->dosbus());
  uint32_t compressed_body_size =
      ComputeCompressedBodySize(width, height, !bit_depth_8);
  uint32_t compressed_header_size =
      ComputeCompressedHeaderSize(width, height, !bit_depth_8);

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

  uint32_t frame_buffer_size =
      fbl::round_up(compressed_body_size, static_cast<uint32_t>(PAGE_SIZE));
  if (!io_buffer_is_valid(&current_frame_->compressed_data) ||
      (io_buffer_size(&current_frame_->compressed_data, 0) !=
       frame_buffer_size)) {
    if (io_buffer_is_valid(&current_frame_->compressed_data))
      io_buffer_release(&current_frame_->compressed_data);
    zx_status_t status =
        io_buffer_init(&current_frame_->compressed_data, owner_->bti(),
                       frame_buffer_size, IO_BUFFER_RW);
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
                          frame_buffer_size);
    BarrierAfterFlush();
  }

  // Enough frames for the maximum possible size of compressed video have to be
  // allocated ahead of time. The hardware will read them from
  // frame_map_mmu.buffer as needed.
  //
  // TODO(MTWN-148): Return unused frames could be returned to a pool and use
  // them for decoding a different frame.
  {
    uint32_t frame_count = frame_buffer_size / PAGE_SIZE;
    uint32_t* mmu_data = static_cast<uint32_t*>(
        io_buffer_virt(&working_buffers_.frame_map_mmu.buffer()));
    ZX_DEBUG_ASSERT(frame_count * 4 <= working_buffers_.frame_map_mmu.size());
    for (uint32_t i = 0; i < frame_count; i++) {
      ZX_DEBUG_ASSERT(current_frame_->compressed_data.phys_list[i] != 0);
      mmu_data[i] = current_frame_->compressed_data.phys_list[i] >> 12;
    }
    io_buffer_cache_flush(&working_buffers_.frame_map_mmu.buffer(), 0,
                          frame_count * 4);
    BarrierAfterFlush();
  }

  uint32_t buffer_address =
      truncate_to_32(current_frame_->frame->buffer.phys_list[0]);

  HevcSaoYStartAddr::Get().FromValue(buffer_address).WriteTo(owner_->dosbus());
  HevcSaoYWptr::Get().FromValue(buffer_address).WriteTo(owner_->dosbus());
  HevcSaoCStartAddr::Get()
      .FromValue(buffer_address + current_frame_->frame->uv_plane_offset)
      .WriteTo(owner_->dosbus());
  HevcSaoCWptr::Get()
      .FromValue(buffer_address + current_frame_->frame->uv_plane_offset)
      .WriteTo(owner_->dosbus());

  // There's no way to specify a different stride than the default.
  HevcSaoYLength::Get()
      .FromValue(current_frame_->frame->stride * height)
      .WriteTo(owner_->dosbus());
  HevcSaoCLength::Get()
      .FromValue(current_frame_->frame->stride * height / 2)
      .WriteTo(owner_->dosbus());
  // Compressed data is used as a reference for future frames, and uncompressed
  // data is output to consumers. Uncompressed data writes could be disabled in
  // the future if the consumer (e.g. the display) supported reading the
  // compressed data.
  {
    auto temp = HevcSaoCtrl1::Get().ReadFrom(owner_->dosbus());
    temp.set_mem_map_mode(HevcSaoCtrl1::kMemMapModeLinear)
        .set_endianness(HevcSaoCtrl1::kBigEndian64);
    if (IsDeviceAtLeast(owner_->device_type(), DeviceType::kG12A)) {
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

bool Vp9Decoder::CanBeSwappedIn() {
  bool has_available_output_frames = false;
  for (uint32_t i = 0; i < frames_.size(); i++) {
    if (frames_[i]->refcount == 0) {
      has_available_output_frames = true;
      break;
    }
  }
  if (!has_available_output_frames)
    return false;

  if (check_output_ready_ && !check_output_ready_()) {
    return false;
  }

  return frame_data_provider_->HasMoreInputData();
}

void Vp9Decoder::ShowExistingFrame(HardwareRenderParams* params) {
  Frame* frame = reference_frame_map_[params->frame_to_show];
  if (!frame) {
    DECODE_ERROR("Showing existing frame that doesn't exist");
    return;
  }
  // stream_offset points to an offset within the header of the frame. With
  // superframes, the offset stored in the PTS manager will be the start of the
  // superframe, but since the offset here is less than the start of the next
  // superframe the correct PTS will be found.
  //
  // When show_existing_frame is set, the original PTS from when the reference
  // frame was decoded is ignored.
  uint32_t stream_offset =
      HevcShiftByteCount::Get().ReadFrom(owner_->dosbus()).reg_value();

  PtsManager::LookupResult result = pts_manager_->Lookup(stream_offset);
  frame->frame->has_pts = result.has_pts();
  frame->frame->pts = result.pts();
  if (result.is_end_of_stream()) {
    // TODO(dustingreen): Handle this once we're able to detect this way.  For
    // now, ignore but print an obvious message.
    printf("##### UNHANDLED END OF STREAM DETECTED #####\n");
    return;
  }

  if (notifier_) {
    frame->refcount++;
    notifier_(frame->frame);
  }
  ZX_DEBUG_ASSERT(state_ == DecoderState::kPausedAtHeader);
  HevcDecStatusReg::Get()
      .FromValue(kVp9CommandDecodeSlice)
      .WriteTo(owner_->dosbus());
  state_ = DecoderState::kRunning;
}

void Vp9Decoder::PrepareNewFrame() {
  if (check_output_ready_ && !check_output_ready_()) {
    // Becomes false when ReturnFrame() gets called, at which point
    // PrepareNewFrame() gets another chance to check again and set back to true
    // as necessary.  This bool needs to exist only so that ReturnFrame() can
    // know whether the decoder is currently needing PrepareNewFrame().
    waiting_for_output_ready_ = true;
    return;
  }

  HardwareRenderParams params;
  BarrierBeforeInvalidate();
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

  if (params.show_existing_frame) {
    ShowExistingFrame(&params);
    return;
  }

  // If this is failing due to running out of buffers then the function will be
  // retried once more are received.
  if (!FindNewFrameBuffer(&params))
    return;

  last_frame_data_ = current_frame_data_;
  // See comments about stream_offset above. Multiple frames will return the
  // same PTS if they're part of a superframe, but only one of the frames should
  // have show_frame set, so only that frame will be output with that PTS.
  uint32_t stream_offset =
      HevcShiftByteCount::Get().ReadFrom(owner_->dosbus()).reg_value();

  PtsManager::LookupResult result = pts_manager_->Lookup(stream_offset);
  current_frame_data_.has_pts = result.has_pts();
  current_frame_data_.pts = result.pts();
  if (result.is_end_of_stream()) {
    // TODO(dustingreen): Handle this once we're able to detect this way.  For
    // now, ignore but print an obvious message.
    printf("##### UNHANDLED END OF STREAM DETECTED #####\n");
    return;
  }

  current_frame_data_.keyframe = params.frame_type == 0;
  current_frame_data_.intra_only = params.intra_only;
  current_frame_data_.refresh_frame_flags = params.refresh_frame_flags;
  if (current_frame_data_.keyframe) {
    current_frame_data_.refresh_frame_flags =
        (1 << fbl::count_of(reference_frame_map_)) - 1;
  }
  current_frame_data_.error_resilient_mode = params.error_resilient_mode;
  current_frame_data_.show_frame = params.show_frame;

  SetRefFrames(&params);

  uint32_t width = params.width;
  uint32_t height = params.height;
  HevcParserPictureSize::Get()
      .FromValue((height << 16) | width)
      .WriteTo(owner_->dosbus());

  ConfigureReferenceFrameHardware();
  ConfigureMotionPrediction();
  ConfigureMcrcc();

  ConfigureFrameOutput(width, height, params.bit_depth == 8);

  UpdateLoopFilter(&params);

  ZX_DEBUG_ASSERT(state_ == DecoderState::kPausedAtHeader);
  HevcDecStatusReg::Get()
      .FromValue(kVp9CommandDecodeSlice)
      .WriteTo(owner_->dosbus());
  state_ = DecoderState::kRunning;
}

void Vp9Decoder::SetFrameReadyNotifier(FrameReadyNotifier notifier) {
  notifier_ = std::move(notifier);
}

void Vp9Decoder::SetCheckOutputReady(CheckOutputReady check_output_ready) {
  check_output_ready_ = std::move(check_output_ready);
}

Vp9Decoder::Frame::~Frame() {
  io_buffer_release(&compressed_header);
  io_buffer_release(&compressed_data);
}

bool Vp9Decoder::FindNewFrameBuffer(HardwareRenderParams* params) {
  assert(!current_frame_);
  ZX_DEBUG_ASSERT(!waiting_for_empty_frames_);
  Frame* new_frame = nullptr;
  for (uint32_t i = 0; i < frames_.size(); i++) {
    if (frames_[i]->refcount == 0) {
      new_frame = frames_[i].get();
      break;
    }
  }
  if (!new_frame) {
    waiting_for_empty_frames_ = true;
    DLOG("Couldn't allocate framebuffer - all in use\n");
    return false;
  }

  uint32_t display_width, display_height;
  if (params->render_size_present) {
    display_width = params->render_width;
    display_height = params->render_height;
  } else {
    display_width = params->width;
    display_height = params->height;
  }
  // TODO: keep old frames that are larger than the new frame size, to avoid
  // reallocating as often.
  if (!new_frame->frame || (new_frame->frame->width != params->width) ||
      (new_frame->frame->height != params->height)) {
    BarrierBeforeRelease();
    // It's simplest to allocate all frames at once on resize, though that can
    // cause frames that should have been output to not be output if a
    // show_existing_frame after the resize wants to show a pre-resize frame, or
    // if the reallocate leads to reference frames that aren't available to use
    // for constructing a frame.
    //
    // We care that the decoder doesn't crash across buffer reallocation, and
    // that it re-synchronizes with the stream after a while (doesn't refuse to
    // deliver output frames forever), but we don't (so far) care that frames
    // can be dropped when resolution switching also involves re-allocating
    // buffers.
    //
    // TODO(dustingreen): When buffers are large enough and aren't reallocated,
    // resolution switching should be seamless.  See also TODO above re. keeping
    // larger buffers if the needed buffer size goes down.
    //
    // The reason for having a higher bar for degree of seamless-ness when
    // buffers are not reallocated (vs. lower-than-"perfect" bar when they are
    // re-allocated) is partly because of the need for phsyically contiguous
    // VMOs and the associated potential for physical memory fragmentation
    // caused by piecemeal buffer allocation and deallocation given an arbitrary
    // VP9 stream that has arbitrary resolution switching and
    // show_existing_frame.  The ability to seamlessly switch/adjust resolution
    // within a buffer set that is large enough to support the max resolution of
    // the stream should offer sufficient functionality to avoid causing
    // practical problems for clients, and this bar being set where it is should
    // avoid creating physical fragmentation / excessive physical reservation
    // problems for the overall system.  It also reduces complexity (vs.
    // "perfect") for clients and for codecs without sacrificing resolution
    // switching entirely.  It also avoids assuming that buffers can be
    // dynamically added/removed from a buffer set without creating timing
    // problems (and/or requiring more buffers to compensate for timing effects
    // of dynamic add/remove).
    for (uint32_t i = 0; i < frames_.size(); i++) {
      // In normal operation (outside decoder self-tests) this reset() is relied
      // upon to essentially signal to the CodecBuffer::frame weak_ptr<> that
      // ReturnFrame() should no longer be called on this frame.  This implies
      // (for now) that the VideoFrame must not be shared outside transients
      // under video_decoder_lock_.  See comment on Vp9Decoder::Frame::frame for
      // more.
      frames_[i]->frame.reset();
    }

    uint32_t stride = fbl::round_up(params->width, 32u);

    uint32_t frame_vmo_bytes =
        params->height * stride + params->height * stride / 2;
    if (initialize_frames_handler_) {
      ::zx::bti duplicated_bti;
      zx_status_t dup_result =
          ::zx::unowned_bti(owner_->bti())
              ->duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicated_bti);
      if (dup_result != ZX_OK) {
        DECODE_ERROR("Failed to duplicate BTI - status: %d\n", dup_result);
        return false;
      }
      // VP9 doesn't have sample_aspect_ratio at ES (.ivf) layer, so here we
      // report "false, 1, 1" to indicate that the ES doesn't have a
      // sample_aspect_ratio.  The Codec client may potentially obtain
      // sample_aspect_ratio from other sources such as a .webm container. If
      // those potential sources don't provide sample_aspect_ratio, then 1:1 is
      // a reasonable default.
      zx_status_t initialize_result = initialize_frames_handler_(
          std::move(duplicated_bti), frames_.size(), params->width,
          params->height, stride, display_width, display_height, false, 1, 1);
      if (initialize_result != ZX_OK) {
        if (initialize_result != ZX_ERR_STOP) {
          DECODE_ERROR("initialize_frames_handler_() failed - status: %d\n",
                       initialize_result);
        }
        return false;
      }
      waiting_for_empty_frames_ = true;
      return false;
    } else {
      std::vector<CodecFrame> frames;
      for (uint32_t i = 0; i < frames_.size(); i++) {
        ::zx::vmo frame_vmo;
        zx_status_t vmo_create_result =
            zx_vmo_create_contiguous(owner_->bti(), frame_vmo_bytes, 0,
                                     frame_vmo.reset_and_get_address());
        if (vmo_create_result != ZX_OK) {
          DECODE_ERROR("zx_vmo_create_contiguous failed - status: %d\n",
                       vmo_create_result);
          return false;
        }
        fuchsia::media::StreamBufferData codec_buffer_data;
        fuchsia::media::StreamBufferDataVmo data_vmo;
        data_vmo.set_vmo_handle(std::move(frame_vmo));
        data_vmo.set_vmo_usable_start(0);
        data_vmo.set_vmo_usable_size(frame_vmo_bytes);
        codec_buffer_data.set_vmo(std::move(data_vmo));
        fuchsia::media::StreamBuffer buffer;
        buffer.set_buffer_lifetime_ordinal(
            next_non_codec_buffer_lifetime_ordinal_);
        buffer.set_buffer_index(0);
        buffer.set_data(std::move(codec_buffer_data));
        frames.emplace_back(CodecFrame{
            .codec_buffer_spec = std::move(buffer),
            .codec_buffer_ptr = nullptr,
        });
      }
      next_non_codec_buffer_lifetime_ordinal_++;
      waiting_for_empty_frames_ = true;
      InitializedFrames(std::move(frames), params->width, params->height,
                        stride);
      // InitializedFrames will call back into PrepareNewFrame to actually
      // prepare for the decoding, so this call should return false so that the
      // outer PrepareNewFrame call exits without trying to prepare decoding
      // again.
      return false;
    }
  }

  new_frame->frame->display_width = display_width;
  new_frame->frame->display_height = display_height;

  current_frame_ = new_frame;
  current_frame_->refcount++;
  current_frame_->decoded_index = decoded_frame_count_++;

  if (cached_mpred_buffer_) {
    current_mpred_buffer_ = std::move(cached_mpred_buffer_);
  } else {
    current_mpred_buffer_ = std::make_unique<MpredBuffer>();
    // The largest coding unit is assumed to be 64x32.
    constexpr uint32_t kLcuMvBytes = 0x240;
    constexpr uint32_t kLcuCount = 4096 * 2048 / (64 * 32);
    zx_status_t status = io_buffer_init_aligned(
        &current_mpred_buffer_->mv_mpred_buffer, owner_->bti(),
        kLcuCount * kLcuMvBytes, 16, IO_BUFFER_CONTIG | IO_BUFFER_RW);
    if (status != ZX_OK) {
      DECODE_ERROR("Alloc buffer error: %d\n", status);
      return false;
    }
    io_buffer_cache_flush_invalidate(&current_mpred_buffer_->mv_mpred_buffer, 0,
                                     kLcuCount * kLcuMvBytes);
    BarrierAfterFlush();
  }

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
    zx_status_t status = owner_->AllocateIoBuffer(
        &frame->compressed_header, kCompressedHeaderSize, 16,
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

void Vp9Decoder::SetInitializeFramesHandler(InitializeFramesHandler handler) {
  initialize_frames_handler_ = std::move(handler);
}

void Vp9Decoder::SetErrorHandler(fit::closure error_handler) {
  error_handler_ = std::move(error_handler);
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
  ZX_DEBUG_ASSERT(state_ == DecoderState::kSwappedOut);
  HevcDecStatusReg::Get().FromValue(0).WriteTo(owner_->dosbus());

  HevcIqitScalelutWrAddr::Get().FromValue(0).WriteTo(owner_->dosbus());
  for (uint32_t i = 0; i < 1024; i++) {
    HevcIqitScalelutData::Get().FromValue(0).WriteTo(owner_->dosbus());
  }

  HevcStreamSwapTest::Get().FromValue(0).WriteTo(owner_->dosbus());
  enum DecodeModes {
    kDecodeModeSingle = (0x80 << 24) | 0,
    kDecodeModeMultiStreamBased = (0x80 << 24) | 1,
    kDecodeModeMultiFrameBased = (0x80 << 24) | 2,
  };
  uint32_t decode_mode;
  switch (input_type_) {
    case InputType::kSingleStream:
      decode_mode = kDecodeModeSingle;
      break;
    case InputType::kMultiStream:
      decode_mode = kDecodeModeMultiStreamBased;
      break;
    case InputType::kMultiFrameBased:
      decode_mode = kDecodeModeMultiFrameBased;
      break;
  }
  DecodeMode::Get().FromValue(decode_mode).WriteTo(owner_->dosbus());
  // For multi-stream UpdateDecodeSize() should be called before
  // StartDecoding(), because the hardware treats size 0 as infinite.
  if (input_type_ == InputType::kSingleStream) {
    HevcDecodeSize::Get().FromValue(0).WriteTo(owner_->dosbus());
    HevcDecodeCount::Get().FromValue(0).WriteTo(owner_->dosbus());
  }

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

  if (IsDeviceAtLeast(owner_->device_type(), DeviceType::kG12A)) {
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
