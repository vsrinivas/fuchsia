// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vp9_decoder.h"

#include <lib/media/codec_impl/codec_buffer.h>
#include <lib/media/codec_impl/codec_packet.h>
#include <lib/trace/event.h>

#include <algorithm>
#include <iterator>
#include <memory>

#include <fbl/algorithm.h>

#include "device_type.h"
#include "firmware_blob.h"
#include "macros.h"
#include "pts_manager.h"
#include "src/media/lib/memory_barriers/memory_barriers.h"
#include "src/media/lib/metrics/metrics.cb.h"
#include "third_party/libvpx/vp9/common/vp9_loopfilter.h"
#include "third_party/vp9_adapt_probs/vp9_coefficient_adaptation.h"
#include "util.h"
#include "vp9_configuration.h"
#include "vp9_utils.h"
#include "watchdog.h"

namespace amlogic_decoder {

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

const char* Vp9Decoder::DecoderStateName(DecoderState state) {
  switch (state) {
    case DecoderState::kInitialWaitingForInput:
      return "InitialWaitingForInput";
    case DecoderState::kStoppedWaitingForInput:
      return "StoppedWaitingForInput";
    case DecoderState::kFrameJustProduced:
      return "FrameJustProduced";
    case DecoderState::kRunning:
      return "Running";
    case DecoderState::kPausedAtHeader:
      return "PausedAtHeader";
    case DecoderState::kPausedAtEndOfStream:
      return "PausedAtEndOfStream";
    case DecoderState::kSwappedOut:
      return "SwappedOut";
    case DecoderState::kFailed:
      return "Failed";
    default:
      return "UNKNOWN";
  }
}

// The hardware takes some uncompressed header information and stores it in this structure.
union Vp9Decoder::HardwareRenderParams {
  uint16_t data_words[0x80];
  struct {
    uint16_t profile;
    uint16_t show_existing_frame;
    uint16_t frame_to_show;  // If show_existing frame is 1.
    uint16_t frame_type;     // 0 is kVp9FrameTypeKeyFrame, 1 is kVp9FrameTypeNonKeyFrame
    uint16_t show_frame;
    uint16_t error_resilient_mode;
    uint16_t intra_only;
    uint16_t render_size_present;
    uint16_t reset_frame_context;
    uint16_t refresh_frame_flags;
    uint16_t hw_width;
    uint16_t hw_height;
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
//
// Non-zero values only work when !is_secure, as we can't use REE CPU to check
// for overrun of a secure buffer.
constexpr uint32_t kBufferOverrunPaddingBytes = 0;

// The VP9 format needs 8 reference pictures, plus 1 to decode into.
//
// Extras for use later in the pipeline can be obtained by those participants
// later in the pipeline specifying min_buffer_count_for_camping to sysmem.
constexpr uint32_t kMinFrames = 8 + 1;

// In typical cases we'll use a frame count closer to kMinFrames than
// kMaxFrames, but some specialized scenarios can benefit from more frames.
constexpr uint32_t kMaxFrames = 24;

void Vp9Decoder::BufferAllocator::Register(WorkingBuffer* buffer) { buffers_.push_back(buffer); }

zx_status_t Vp9Decoder::BufferAllocator::AllocateBuffers(VideoDecoder::Owner* owner,
                                                         bool is_secure) {
  ZX_DEBUG_ASSERT(kBufferOverrunPaddingBytes == 0 || !is_secure);
  for (auto* buffer : buffers_) {
    bool buffer_is_secure = is_secure && buffer->can_be_protected();
    uint64_t rounded_up_size = fbl::round_up(buffer->size() + kBufferOverrunPaddingBytes,
                                             static_cast<uint32_t>(PAGE_SIZE));
    auto internal_buffer =
        InternalBuffer::Create(buffer->name(), &owner->SysmemAllocatorSyncPtr(), owner->bti(),
                               rounded_up_size, buffer_is_secure,
                               /*is_writable=*/true, /*is_mapping_needed=*/!buffer_is_secure);
    if (!internal_buffer.is_ok()) {
      LOG(ERROR, "VP9 working buffer allocation failed: %d", internal_buffer.error());
      return internal_buffer.error();
    }
    buffer->SetBuffer(internal_buffer.take_value());
    if (kBufferOverrunPaddingBytes) {
      uint32_t real_buffer_size = buffer->buffer().size();
      for (uint32_t i = buffer->size(); i < real_buffer_size; i++) {
        uint8_t* data = buffer->buffer().virt_base();
        data[i] = i & 0xff;
      }
    }
    buffer->buffer().CacheFlushInvalidate(0, buffer->size() + kBufferOverrunPaddingBytes);
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
      if (!buffer->has_buffer())
        continue;
      uint32_t offset = buffer->size();
      uint8_t* data = buffer->buffer().virt_base();
      uint32_t buffer_size = buffer->buffer().size();
      buffer->buffer().CacheFlushInvalidate(offset, buffer_size - offset);
      for (uint32_t i = offset; i < buffer_size; ++i) {
        if (data[i] != (i & 0xff)) {
          LOG(ERROR, "Data mismatch: %d != %d in buffer %d position %d", data[i], (i & 0xff),
              buf_number, i);
        }
        ZX_DEBUG_ASSERT(data[i] == (i & 0xff));
      }
      buffer->buffer().CacheFlushInvalidate(offset, buffer_size - offset);
    }
  }
}

Vp9Decoder::WorkingBuffer::WorkingBuffer(BufferAllocator* allocator, size_t size,
                                         bool can_be_protected, const char* name)
    : size_(size), can_be_protected_(can_be_protected), name_(name) {
  allocator->Register(this);
}

Vp9Decoder::WorkingBuffer::~WorkingBuffer() {}

uint32_t Vp9Decoder::WorkingBuffer::addr32() { return truncate_to_32(buffer_->phys_base()); }

Vp9Decoder::Vp9Decoder(Owner* owner, Client* client, InputType input_type,
                       std::optional<InternalBuffers> internal_buffers, bool use_compressed_output,
                       bool is_secure)
    : VideoDecoder(
          media_metrics::
              StreamProcessorEvents2MigratedMetricDimensionImplementation_AmlogicDecoderVp9,
          owner, client, is_secure),
      input_type_(input_type),
      use_compressed_output_(use_compressed_output) {
  constexpr uint32_t kStreamOffsetBitWidth = 32;
  pts_manager_->SetLookupBitWidth(kStreamOffsetBitWidth);
  // Compressed output buffers can't yet be allocated in secure memory.
  assert(!is_secure || !use_compressed_output_);
  InitializeLoopFilterData();
  power_ref_ = std::make_unique<PowerReference>(owner_->hevc_core());
  if (internal_buffers.has_value()) {
    GiveInternalBuffers(std::move(*internal_buffers));
  }
}

void Vp9Decoder::ForceStopDuringRemoveLocked() {
  ZX_DEBUG_ASSERT(owner_->IsDecoderCurrent(this));
  owner_->core()->StopDecoding();
  owner_->core()->WaitForIdle();
  owner_->watchdog()->Cancel();
  working_buffers_->CheckBuffers();
}

Vp9Decoder::~Vp9Decoder() {
  // We can't do working_buffers_->CheckBuffers() here since they've already been moved out, but we
  // do working_buffers_->CheckBuffers() just before buffers are moved out, and also just after
  // force-stopping the decoder if ForceStopDuringRemoveLocked() gets called, but that only happens
  // when the decoder is current at the time it's removed.
}

void Vp9Decoder::UpdateLoopFilterThresholds() {
  for (uint32_t i = 0; i <= MAX_LOOP_FILTER / 2; i++) {
    uint32_t threshold = 0;
    for (uint32_t j = 0; j < 2; j++) {
      uint32_t new_threshold = ((loop_filter_info_->lfthr[i * 2 + j].lim[0] & 0x3f) << 8) |
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
  loop_filter_->mode_ref_delta_enabled = static_cast<uint8_t>(param->mode_ref_delta_enabled);
  loop_filter_->sharpness_level = param->sharpness_level;
  for (uint32_t i = 0; i < std::size(param->ref_deltas); i++)
    loop_filter_->ref_deltas[i] = static_cast<uint8_t>(param->ref_deltas[i]);
  for (uint32_t i = 0; i < std::size(param->mode_deltas); i++)
    loop_filter_->mode_deltas[i] = static_cast<uint8_t>(param->mode_deltas[i]);

  segmentation_->enabled = static_cast<uint8_t>(param->segmentation_enabled);
  segmentation_->abs_delta = static_cast<uint8_t>(param->segmentation_abs_delta);
  for (uint32_t i = 0; i < MAX_SEGMENTS; i++) {
    segmentation_->feature_mask[i] =
        (param->segmentation_loop_filter_info[i] & 0x8000) ? (1 << SEG_LVL_ALT_LF) : 0;
    uint16_t abs_value = param->segmentation_loop_filter_info[i] & 0x3f;
    segmentation_->feature_data[i][SEG_LVL_ALT_LF] =
        (param->segmentation_loop_filter_info[i] & 0x100) ? -abs_value : abs_value;
  }
  bool updated_sharpness;
  vp9_loop_filter_frame_init(loop_filter_.get(), loop_filter_info_.get(), segmentation_.get(),
                             param->filter_level, &updated_sharpness);
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
  TRACE_DURATION("media", "Vp9Decoder::Initialize");
  zx_status_t status = InitializeBuffers();
  if (status != ZX_OK) {
    LogEvent(media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_InitializationError);
    LOG(ERROR, "InitializeBuffers() failed");
    return status;
  }
  return InitializeHardware();
}

zx_status_t Vp9Decoder::InitializeBuffers() {
  TRACE_DURATION("media", "Vp9Decoder::InitializeBuffers");

  //
  // working buffers
  //

  std::optional<WorkingBuffers> working_buffers;
  ZX_DEBUG_ASSERT(!on_deck_internal_buffers_.has_value() ||
                  !on_deck_internal_buffers_->working_buffers_.has_value() ||
                  (on_deck_internal_buffers_->working_buffers_->rpm.has_buffer() &&
                   on_deck_internal_buffers_->working_buffers_->rpm.buffer().present()));
  if (on_deck_internal_buffers_.has_value() &&
      on_deck_internal_buffers_->working_buffers_.has_value() &&
      on_deck_internal_buffers_->working_buffers_->short_term_rps.buffer().is_secure() !=
          is_secure()) {
    on_deck_internal_buffers_->working_buffers_.reset();
  }
  if (on_deck_internal_buffers_.has_value() &&
      on_deck_internal_buffers_->working_buffers_.has_value()) {
    ZX_DEBUG_ASSERT(
        on_deck_internal_buffers_->working_buffers_->short_term_rps.buffer().is_secure() ==
        is_secure());
    working_buffers = std::move(on_deck_internal_buffers_->working_buffers_);
  } else {
    working_buffers.emplace();
    zx_status_t status = working_buffers->AllocateBuffers(owner_, is_secure());
    if (status != ZX_OK) {
      LogEvent(media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_AllocationError);
      LOG(ERROR, "working_buffers_->AllocateBuffers() failed");
      return status;
    }
    ZX_DEBUG_ASSERT(working_buffers.has_value());
    ZX_DEBUG_ASSERT(working_buffers->rpm.has_buffer());
    ZX_DEBUG_ASSERT(working_buffers->rpm.buffer().present());
  }
  // Only assign if all working buffers got allocated, to make sure it's impossible to ever have
  // InternalBuffers.working_buffers_ that's only got some of the working buffers allocated.
  working_buffers_ = std::move(working_buffers);

  //
  // frames
  //

  zx_status_t status = AllocateFrames();
  if (status != ZX_OK) {
    LogEvent(media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_AllocationError);
    LOG(INFO, "AllocateFrames() failed - status: %d", status);
    return status;
  }

  //
  // mpred buffers
  //

  // We need a small fixed # of mpred buffers to avoid ever needing to allocate an mpred buffer
  // while decoding.
  constexpr uint32_t kNumMpredBuffers = 2;
  // We've already put any previous-instance mpred buffers in cached_mpred_buffers_; they aren't
  // still in on_deck_internal_buffers_.
  ZX_DEBUG_ASSERT(!on_deck_internal_buffers_.has_value() ||
                  on_deck_internal_buffers_->mpred_buffers_.empty());
  // The largest coding unit is assumed to be 64x32.
  constexpr uint32_t kLcuMvBytes = 0x240;
  // Round up 1080 to 1088 and 2160 to 2176 so that all dimensions are divisible by 64, in case of
  // decoding a portrait mode video.
  constexpr uint32_t kLcuCount = kUseLessRam ? 1920 * 1088 / (64 * 32) : 4096 * 2176 / (64 * 32);
  uint64_t rounded_up_size =
      fbl::round_up(kLcuCount * kLcuMvBytes, static_cast<uint64_t>(PAGE_SIZE));
  constexpr uint32_t kMpredAlignment = (1 << 16);
  constexpr bool kMpredIsWritable = true;
  constexpr bool kMpredIsMappingNeeded = false;
  // We'll likely end up moving these back into cached_mpred_buffers_, after we make sure their
  // properties are suitable.
  std::vector<std::unique_ptr<InternalBuffer>> cached_mpred_buffers(
      std::move(cached_mpred_buffers_));
  ZX_DEBUG_ASSERT(cached_mpred_buffers_.empty());
  for (uint32_t i = 0; i < kNumMpredBuffers; ++i) {
    std::unique_ptr<InternalBuffer> mpred_buffer;
    if (!cached_mpred_buffers.empty() && cached_mpred_buffers.back()->is_secure() != is_secure()) {
      cached_mpred_buffers.clear();
    }
    if (!cached_mpred_buffers.empty()) {
      auto cached_mpred_buffer = std::move(cached_mpred_buffers.back());
      cached_mpred_buffers.pop_back();
      ZX_DEBUG_ASSERT(cached_mpred_buffer->size() == rounded_up_size);
      ZX_DEBUG_ASSERT(cached_mpred_buffer->alignment() == kMpredAlignment);
      ZX_DEBUG_ASSERT(cached_mpred_buffer->is_secure() == is_secure());
      ZX_DEBUG_ASSERT(cached_mpred_buffer->is_writable() == kMpredIsWritable);
      ZX_DEBUG_ASSERT(cached_mpred_buffer->is_mapping_needed() == kMpredIsMappingNeeded);
      mpred_buffer = std::move(cached_mpred_buffer);
    } else {
      auto internal_buffer = InternalBuffer::CreateAligned(
          "Vp9MpredData", &owner_->SysmemAllocatorSyncPtr(), owner_->bti(), rounded_up_size,
          kMpredAlignment, is_secure(), /*is_writable=*/kMpredIsWritable,
          /*is_mapping_needed=*/kMpredIsMappingNeeded);
      if (!internal_buffer.is_ok()) {
        LogEvent(media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_AllocationError);
        LOG(ERROR, "Alloc buffer error: %d", internal_buffer.error());
        return internal_buffer.error();
      }
      mpred_buffer = std::make_unique<InternalBuffer>(internal_buffer.take_value());
    }
    ZX_DEBUG_ASSERT(mpred_buffer && mpred_buffer->present());
    mpred_buffer->CacheFlushInvalidate(0, rounded_up_size);
    cached_mpred_buffers_.emplace_back(std::move(mpred_buffer));
    ZX_DEBUG_ASSERT(cached_mpred_buffers_.back() && cached_mpred_buffers_.back()->present());
  }
  // If this ever triggers, why do we need more?
  ZX_ASSERT(cached_mpred_buffers.empty());
  // If somehow we end up with extras, those will be deleted when ~cached_mpred_buffers during
  // return below.

  // For all frames and working buffers (for those that didn't already flush or invalidate, if any;
  // a flush or invalidate includes the relevant barrier(s)).
  BarrierAfterFlush();

  return status;
}

zx_status_t Vp9Decoder::InitializeHardware() {
  TRACE_DURATION("media", "Vp9Decoder::InitializeHardware");
  ZX_DEBUG_ASSERT(state_ == DecoderState::kSwappedOut);
  assert(owner_->IsDecoderCurrent(this));
  working_buffers_->CheckBuffers();
  zx_status_t status =
      owner_->SetProtected(VideoDecoder::Owner::ProtectableHardwareUnit::kHevc, is_secure());
  if (status != ZX_OK) {
    LogEvent(media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_DrmConfigError);
    LOG(ERROR, "SetProtected(kHevc) failed");
    return status;
  }
  if (should_inject_initialization_fault_for_testing_) {
    should_inject_initialization_fault_for_testing_ = false;
    return ZX_ERR_BAD_STATE;
  }

  if (owner_->is_tee_available()) {
    status = owner_->TeeSmcLoadVideoFirmware(FirmwareBlob::FirmwareType::kDec_Vp9_Mmu,
                                             FirmwareBlob::FirmwareVdecLoadMode::kHevc);
    if (status != ZX_OK) {
      LogEvent(media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_FirmwareLoadError);
      LOG(ERROR, "owner_->TeeSmcLoadVideoFirmware() failed - status: %d", status);
      return status;
    }
  } else {
    if (is_secure()) {
      LogEvent(media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_DrmConfigError);
      LOG(ERROR, "VP9 secure decode requires TEE connection");
      return ZX_ERR_NOT_SUPPORTED;
    }
    uint8_t* data;
    uint32_t firmware_size;
    // TODO(fxbug.dev/43496): In "CL3", we'll filter video_ucode.bin firmwares by current device,
    // which will let loading by this more generic ID work, assuming new video_ucode.bin.  For now,
    // if we were to take this path (which we don't), this would likely get the wrong firmware.
    status = owner_->firmware_blob()->GetFirmwareData(FirmwareBlob::FirmwareType::kDec_Vp9_Mmu,
                                                      &data, &firmware_size);
    if (status != ZX_OK) {
      LogEvent(media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_FirmwareLoadError);
      LOG(ERROR, "GetFirmwareData() failed");
      return status;
    }
    status = owner_->core()->LoadFirmware(data, firmware_size);
    if (status != ZX_OK) {
      LogEvent(media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_FirmwareLoadError);
      LOG(ERROR, "LoadFirmware() failed");
      return status;
    }
  }

  HevcRpmBuffer::Get().FromValue(working_buffers_->rpm.addr32()).WriteTo(owner_->dosbus());
  HevcShortTermRps::Get()
      .FromValue(working_buffers_->short_term_rps.addr32())
      .WriteTo(owner_->dosbus());
  HevcPpsBuffer::Get()
      .FromValue(working_buffers_->picture_parameter_set.addr32())
      .WriteTo(owner_->dosbus());
  HevcStreamSwapBuffer::Get().FromValue(working_buffers_->swap.addr32()).WriteTo(owner_->dosbus());
  HevcStreamSwapBuffer2::Get()
      .FromValue(working_buffers_->swap2.addr32())
      .WriteTo(owner_->dosbus());
  HevcLmemDumpAdr::Get()
      .FromValue(working_buffers_->local_memory_dump.addr32())
      .WriteTo(owner_->dosbus());
  HevcdIppLinebuffBase::Get()
      .FromValue(working_buffers_->ipp_line_buffer.addr32())
      .WriteTo(owner_->dosbus());
  HevcSaoUp::Get().FromValue(working_buffers_->sao_up.addr32()).WriteTo(owner_->dosbus());
  HevcScaleLut::Get().FromValue(working_buffers_->scale_lut.addr32()).WriteTo(owner_->dosbus());

  if (IsDeviceAtLeast(owner_->device_type(), DeviceType::kG12A)) {
    HevcDblkCfgE::Get()
        .FromValue(working_buffers_->deblock_parameters2.addr32())
        .WriteTo(owner_->dosbus());
  }

  // The linux driver doesn't write to this register on G12A, but that seems to
  // cause the hardware to write some data to physical address 0 and corrupt
  // memory.
  HevcDblkCfg4::Get()
      .FromValue(working_buffers_->deblock_parameters.addr32())
      .WriteTo(owner_->dosbus());

  // The firmware expects the deblocking data to always follow the parameters.
  HevcDblkCfg5::Get()
      .FromValue(working_buffers_->deblock_parameters.addr32() +
                 WorkingBuffers::kDeblockParametersSize)
      .WriteTo(owner_->dosbus());

  if (use_compressed_output_) {
    HevcdMppDecompCtl1::Get().FromValue(0).set_paged_mode(1).WriteTo(owner_->dosbus());
  } else {
    HevcdMppDecompCtl1::Get().FromValue(0).set_use_uncompressed(1).WriteTo(owner_->dosbus());
  }
  HevcdMppDecompCtl2::Get().FromValue(0).WriteTo(owner_->dosbus());

  if (use_compressed_output_) {
    HevcSaoMmuVh0Addr::Get()
        .FromValue(working_buffers_->mmu_vbh.addr32())
        .WriteTo(owner_->dosbus());
    HevcSaoMmuVh1Addr::Get()
        .FromValue(truncate_to_32(working_buffers_->mmu_vbh.addr32() +
                                  working_buffers_->mmu_vbh.size() / 2))
        .WriteTo(owner_->dosbus());
    HevcSaoCtrl5::Get()
        .ReadFrom(owner_->dosbus())
        .set_use_compressed_header(1)
        .WriteTo(owner_->dosbus());
  }

  Vp9SegMapBuffer::Get()
      .FromValue(working_buffers_->segment_map.addr32())
      .WriteTo(owner_->dosbus());
  Vp9ProbSwapBuffer::Get()
      .FromValue(working_buffers_->probability_buffer.addr32())
      .WriteTo(owner_->dosbus());
  Vp9CountSwapBuffer::Get()
      .FromValue(working_buffers_->count_buffer.addr32())
      .WriteTo(owner_->dosbus());

  if (use_compressed_output_) {
    if (IsDeviceAtLeast(owner_->device_type(), DeviceType::kG12A)) {
      HevcAssistMmuMapAddr::Get()
          .FromValue(working_buffers_->frame_map_mmu.addr32())
          .WriteTo(owner_->dosbus());
    } else {
      Vp9MmuMapBuffer::Get()
          .FromValue(working_buffers_->frame_map_mmu.addr32())
          .WriteTo(owner_->dosbus());
    }
  }

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
    owner_->watchdog()->Start();
  } else {
    state_ = DecoderState::kInitialWaitingForInput;
  }
  DLOG("Initialized decoder");
  return ZX_OK;
}

static uint32_t ComputeCompressedBodySize(uint32_t width, uint32_t height, bool is_10_bits) {
  uint32_t block_width = fbl::round_up(width, 64u) / 64;
  uint32_t block_height = fbl::round_up(height, 32u) / 32;
  uint32_t bytes_per_block = is_10_bits ? 4096 : 3200;
  return block_width * block_height * bytes_per_block;
}

static uint32_t ComputeCompressedHeaderSize(uint32_t width, uint32_t height, bool is_10_bits) {
  // Header blocks are twice the size of body blocks.
  uint32_t block_width = fbl::round_up(width, 128u) / 128;
  uint32_t block_height = fbl::round_up(height, 64u) / 64;
  constexpr uint32_t kBytesPerBlock = 32;
  return block_width * block_height * kBytesPerBlock;
}

void Vp9Decoder::Frame::Deref() {
  refcount--;
  assert(refcount >= client_refcount);
  assert(refcount >= 0);
  if (on_deck_frame || index >= parent->valid_frames_count_) {
    // Now that there's an on deck frame that can be decoded into, this frame is
    // just wasting space.
    //
    // Or same if there are fewer frames we intend to actively use going forward.
    ReleaseIfNonreference();
  }
}

void Vp9Decoder::Frame::ReleaseIfNonreference() {
  // If the client's still using the frame it will essentially take ownership of the VMO from this
  // point. The client should never call ReturnFrame on it after this.
  if (refcount == client_refcount) {
    frame = nullptr;
    refcount = 0;
    client_refcount = 0;
  }
}

void Vp9Decoder::ProcessCompletedFrames() {
  TRACE_DURATION("media", "Vp9Decoder::ProcessCompletedFrames");
  // On the first interrupt no frame will be completed.
  if (!current_frame_) {
    DLOG("!current_frame_");
    return;
  }

  if (current_frame_data_.show_frame) {
    current_frame_->frame->has_pts = current_frame_data_.has_pts;
    current_frame_->frame->pts = current_frame_data_.pts;
    current_frame_->refcount++;
    current_frame_->client_refcount++;
    DLOG("client_->OnFrameReady()");
    client_->OnFrameReady(current_frame_->frame);
  }

  for (uint32_t i = 0; i < std::size(reference_frame_map_); i++) {
    if (current_frame_data_.refresh_frame_flags & (1 << i)) {
      if (reference_frame_map_[i]) {
        reference_frame_map_[i]->Deref();
      }
      DLOG("reference_frame_map_[i] = current_frame_ - i: %u", i);
      reference_frame_map_[i] = current_frame_;
      current_frame_->refcount++;
    }
  }
  for (Frame*& frame : current_reference_frames_) {
    frame = nullptr;
  }
  if (last_frame_) {
    last_frame_->Deref();
  }
  last_frame_ = current_frame_;
  current_frame_ = nullptr;

  if (last_mpred_buffer_) {
    ZX_DEBUG_ASSERT(last_mpred_buffer_);
    ZX_DEBUG_ASSERT(last_mpred_buffer_->present());
    cached_mpred_buffers_.emplace_back(std::move(last_mpred_buffer_));
    ZX_DEBUG_ASSERT(!last_mpred_buffer_);
    ZX_DEBUG_ASSERT(cached_mpred_buffers_.back());
    ZX_DEBUG_ASSERT(cached_mpred_buffers_.back()->present());
  }

  ZX_DEBUG_ASSERT(current_mpred_buffer_);
  ZX_DEBUG_ASSERT(current_mpred_buffer_->present());
  last_mpred_buffer_ = std::move(current_mpred_buffer_);
  ZX_DEBUG_ASSERT(!current_mpred_buffer_);
  ZX_DEBUG_ASSERT(last_mpred_buffer_);
  ZX_DEBUG_ASSERT(last_mpred_buffer_->present());
}

void Vp9Decoder::InitializedFrames(std::vector<CodecFrame> frames, uint32_t coded_width,
                                   uint32_t coded_height, uint32_t stride) {
  TRACE_DURATION("media", "Vp9Decoder::InitializedFrames");
  ZX_DEBUG_ASSERT(state_ == DecoderState::kPausedAtHeader);
  ZX_ASSERT(owner_->IsDecoderCurrent(this));
  ZX_DEBUG_ASSERT(valid_frames_count_ == 0);
  uint32_t frame_vmo_bytes = stride * coded_height * 3 / 2;
  BarrierBeforeInvalidate();
  for (uint32_t i = 0; i < frames.size(); i++) {
    auto video_frame = std::make_shared<VideoFrame>();

    // These are set later in PrepareFrame().
    ZX_DEBUG_ASSERT(video_frame->hw_width == 0);
    ZX_DEBUG_ASSERT(video_frame->hw_height == 0);

    video_frame->coded_width = coded_width;
    video_frame->coded_height = coded_height;
    video_frame->stride = stride;
    video_frame->uv_plane_offset = video_frame->stride * video_frame->coded_height;
    video_frame->index = i;

    video_frame->codec_buffer = frames[i].buffer_ptr();
    if (frames[i].buffer_ptr()) {
      frames[i].buffer_ptr()->SetVideoFrame(video_frame);
    }

    ZX_DEBUG_ASSERT(video_frame->coded_height % 2 == 0);
    zx_status_t status =
        io_buffer_init_vmo(&video_frame->buffer, owner_->bti()->get(),
                           frames[i].buffer_spec().vmo_range.vmo().get(), 0, IO_BUFFER_RW);
    if (status != ZX_OK) {
      LogEvent(
          media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_InitializationError);
      LOG(ERROR, "Failed to io_buffer_init_vmo() for frame - status: %d", status);
      CallErrorHandler();
      return;
    }
    size_t vmo_size = io_buffer_size(&video_frame->buffer, 0);
    if (vmo_size < frame_vmo_bytes) {
      LogEvent(
          media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_InitializationError);
      LOG(ERROR, "Insufficient frame vmo bytes: %ld < %d", vmo_size, frame_vmo_bytes);
      CallErrorHandler();
      return;
    }
    status = io_buffer_physmap(&video_frame->buffer);
    if (status != ZX_OK) {
      LogEvent(
          media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_InitializationError);
      LOG(ERROR, "Failed to io_buffer_physmap - status: %d", status);
      CallErrorHandler();
      return;
    }

    for (uint32_t i = 1; i < vmo_size / PAGE_SIZE; i++) {
      if (video_frame->buffer.phys_list[i - 1] + PAGE_SIZE != video_frame->buffer.phys_list[i]) {
        LogEvent(
            media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_InitializationError);
        LOG(ERROR, "VMO isn't contiguous");
        CallErrorHandler();
        return;
      }
    }

    io_buffer_cache_flush_invalidate(&video_frame->buffer, 0,
                                     io_buffer_size(&video_frame->buffer, 0));
    frames_[i]->on_deck_frame = std::move(video_frame);

    // Apply the initial_usage_count().  The frame needs to be returned this many times before it
    // can be reused, unless buffers are reallocated sooner.
    frames_[i]->client_refcount += frames[i].initial_usage_count();
    frames_[i]->refcount += frames[i].initial_usage_count();
  }
  valid_frames_count_ = truncate_to_32(frames.size());
  BarrierAfterFlush();

  ZX_DEBUG_ASSERT(waiting_for_new_frames_);
  ZX_DEBUG_ASSERT(!waiting_for_empty_frames_);
  waiting_for_new_frames_ = false;
  // Also updates state_.
  DLOG("InitializedFrames PrepareNewFrame...");
  PrepareNewFrame(true);
  DLOG("InitializedFrames PrepareNewFrame done");
}

void Vp9Decoder::ReturnFrame(std::shared_ptr<VideoFrame> frame) {
  TRACE_DURATION("media", "Vp9Decoder::ReturnFrame");
  // If this isn't true, the weak ptr would have signaled the caller that we don't need the frame
  // back any more, so the caller doesn't call ReturnFrame().
  ZX_DEBUG_ASSERT(frame->index < frames_.size());
  auto& ref_frame = frames_[frame->index];
  assert(ref_frame->client_refcount >= 1);
  // Frame must still be valid if the refcount is > 0.
  //
  // We only expect to see ref_frame->on_deck_frame == frame if the decoder was recently deleted and
  // re-created while preserving the same buffers and re-establishing free/busy status per frame
  // (and re-connecting the CodecBuffer to the new VideoFrame).  In that case we do expect to see
  // ReturnFrame(), but ref_frame only has an on_deck_frame, not a ref_frame->frame yet.  It looks
  // as if a frame that's never been emitted yet is being returned, which is a bit strange, but
  // expected in that particular situation.
  assert(ref_frame->frame == frame || !ref_frame->frame && ref_frame->on_deck_frame == frame);
  ref_frame->client_refcount--;
  assert(ref_frame->client_refcount >= 0);
  ref_frame->Deref();

  // If either of these bools is true, we know the decoder isn't running.  It's
  // fine that we don't check here that there's a frame with refcount 0 or check
  // here that the output is ready, because PrepareNewFrame() will re-check
  // both those things, and set the appropriate waiting bool back to true if we
  // still need to wait.
  if (waiting_for_output_ready_ || waiting_for_empty_frames_) {
    ZX_ASSERT(owner_->IsDecoderCurrent(this));
    waiting_for_output_ready_ = false;
    waiting_for_empty_frames_ = false;
    DLOG("ReturnFrame PrepareNewFrame...");
    PrepareNewFrame(true);
    DLOG("ReturnFrame PrepareNewFrame done");
  }
}

enum Vp9Command {
  // Sent from the host to the device after a header has been decoded to say
  // that the compressed frame body should be decoded.
  kVp9CommandDecodeSlice = 5,

  // Presumably this could somehow be used when the host wants to tell the FW to
  // skip a frame, but so far we haven't had any luck getting this command to do
  // what it sounds/looks like.  This definition is here to warn off the next
  // person who might consider trying to get this command to work.  Instead, we
  // just parse the frame header enough to determine whether we have a keyframe
  // or not before we send that input frame to the decoder.  We can do that even
  // for DRM frames (clear portion of header) after some other changes.
  //
  // Don't expect this command to work.  Not presently used in this driver.
  kVp9CommandDiscardNal = 6,

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
  TRACE_DURATION("media", "Vp9Decoder::UpdateDecodeSize", "size", size);
  ZX_DEBUG_ASSERT(state_ == DecoderState::kStoppedWaitingForInput ||
                  state_ == DecoderState::kInitialWaitingForInput);

  frames_since_update_decode_size_ = 0;

  uint32_t old_decode_count = HevcDecodeCount::Get().ReadFrom(owner_->dosbus()).reg_value();
  if (old_decode_count != frame_done_count_) {
    HevcDecodeCount::Get().FromValue(frame_done_count_).WriteTo(owner_->dosbus());
  }

  // When input is not from protected memory, this is the size of a frame including the AMLV header.
  //
  // When input is from protected memory, this is either the size of the frame when not a
  // superframe, or a fake size that has the first frame of the superframe pretend to be larger than
  // it actually is, with every subsequent frame after frame 0 pretending to be size 8.
  ZX_DEBUG_ASSERT(size != 0);
  DLOG("size: 0x%x", size);

  uint32_t old_decode_size = HevcDecodeSize::Get().ReadFrom(owner_->dosbus()).reg_value();
  uint32_t new_decode_size = old_decode_size + size;
  DLOG("old_decode_size: 0x%x size: 0x%x new_decode_size: 0x%x", old_decode_size, size,
       new_decode_size);
  HevcDecodeSize::Get().FromValue(new_decode_size).WriteTo(owner_->dosbus());

  if (state_ == DecoderState::kStoppedWaitingForInput) {
    DLOG("kVp9ActionDone (kStoppedWaitingForInput)");
    HevcDecStatusReg::Get().FromValue(kVp9ActionDone).WriteTo(owner_->dosbus());
  }

  owner_->core()->StartDecoding();
  state_ = DecoderState::kRunning;
  owner_->watchdog()->Start();
}

uint32_t Vp9Decoder::FramesSinceUpdateDecodeSize() {
  ZX_DEBUG_ASSERT(state_ == DecoderState::kStoppedWaitingForInput ||
                  state_ == DecoderState::kInitialWaitingForInput);
  return frames_since_update_decode_size_;
}

void Vp9Decoder::SetPausedAtEndOfStream() {
  ZX_DEBUG_ASSERT(state_ == DecoderState::kPausedAtHeader);
  state_ = DecoderState::kPausedAtEndOfStream;
}

void Vp9Decoder::AdaptProbabilityCoefficients(uint32_t adapt_prob_status) {
  TRACE_DURATION("media", "Vp9Decoder::AdaptProbabilityCoefficients");
  constexpr uint32_t kFrameContextSize = 0x1000;
  constexpr uint32_t kVp9FrameContextCount = 4;
  constexpr uint32_t kProbSize = 496 * 2 * 4;  // 3968 < 4096
  static_assert(kProbSize <= kFrameContextSize);
  if ((adapt_prob_status & 0xff) == 0xfd) {
    // current_frame_data_ still reflects the frame that just finished decoding.
    uint32_t previous_fc = current_frame_data_.keyframe;

    // TODO(dustingreen): (comment from jbauman@) We probably don't need to
    // invalidate the entire buffer, but good enough for now.
    working_buffers_->probability_buffer.buffer().CacheFlushInvalidate(
        0, working_buffers_->probability_buffer.buffer().size());
    working_buffers_->count_buffer.buffer().CacheFlushInvalidate(
        0, working_buffers_->count_buffer.buffer().size());

    uint32_t frame_context_idx = adapt_prob_status >> 8;
    uint8_t* previous_prob_buffer = working_buffers_->probability_buffer.buffer().virt_base() +
                                    frame_context_idx * kFrameContextSize;
    uint8_t* current_prob_buffer = working_buffers_->probability_buffer.buffer().virt_base() +

                                   kVp9FrameContextCount * kFrameContextSize;
    uint8_t* count_buffer = working_buffers_->count_buffer.buffer().virt_base();

    adapt_coef_proc_cfg config{};
    config.pre_pr_buf = reinterpret_cast<unsigned int*>(previous_prob_buffer);
    config.pr_buf = reinterpret_cast<unsigned int*>(current_prob_buffer);
    config.count_buf = reinterpret_cast<unsigned int*>(count_buffer);
    adapt_coef_process(&config, !!last_frame_data_.keyframe, previous_fc, frame_context_idx);
    memcpy(reinterpret_cast<uint8_t*>(config.pre_pr_buf), reinterpret_cast<uint8_t*>(config.pr_buf),
           kProbSize);

    // TODO(dustingreen): (comment from jbauman@) We probably only need to flush
    // the portions of the probability buffer that were modified (and none of
    // the count buffer), but this should be fine for now.
    working_buffers_->probability_buffer.buffer().CacheFlush(
        0, working_buffers_->probability_buffer.buffer().size());
    working_buffers_->count_buffer.buffer().CacheFlush(
        0, working_buffers_->count_buffer.buffer().size());
    Vp9AdaptProbReg::Get().FromValue(0).WriteTo(owner_->dosbus());
  }
}

void Vp9Decoder::HandleInterrupt() {
  TRACE_DURATION("media", "Vp9Decoder::HandleInterrupt");
  DLOG("%p Got VP9 interrupt", this);

  uint32_t dec_status = HevcDecStatusReg::Get().ReadFrom(owner_->dosbus()).reg_value();
  uint32_t adapt_prob_status = Vp9AdaptProbReg::Get().ReadFrom(owner_->dosbus()).reg_value();
  TRACE_INSTANT("media", "decoder status", TRACE_SCOPE_THREAD, "dec_status", dec_status);
  DLOG("Decoder state: %x %x", dec_status, adapt_prob_status);

  HevcAssistMbox0ClrReg::Get().FromValue(1).WriteTo(owner_->dosbus());

  if (state_ != DecoderState::kRunning) {
    LOG(WARNING, "spurious interrupt??? - dec_status: %x adapt_prob_status: %x state_: %u",
        dec_status, adapt_prob_status, static_cast<unsigned int>(state_));
    return;
  }

  owner_->watchdog()->Cancel();

  AdaptProbabilityCoefficients(adapt_prob_status);

  if (dec_status == kVp9InputBufferEmpty) {
    // TODO: We'll want to use this to continue filling input data of
    // particularly large input frames, if we can get this to work. Currently
    // attempting to restart decoding after this in frame-based decoding mode
    // causes old data to be skipped.
    LogEvent(
        media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_InputProcessingError);
    LOG(ERROR, "Input buffer empty, insufficient padding?");
    return;
  }

  if (dec_status == kVp9CommandNalDecodeDone) {
    owner_->core()->StopDecoding();
    state_ = DecoderState::kStoppedWaitingForInput;
    HevcDecodeSize::Get().FromValue(0).WriteTo(owner_->dosbus());
    frame_data_provider_->ReadMoreInputData(this);
    return;
  }

  ProcessCompletedFrames();

  if (dec_status == kVp9CommandDecodingDataDone) {
    state_ = DecoderState::kFrameJustProduced;
    frames_since_update_decode_size_++;
    frame_done_count_++;

    owner_->TryToReschedule();

    // The kInitialWaitingForInput part of this condition is for tests where we force swapping out
    // but then swap the decoder back during TryToReschedule() before getting here.  In that case
    // the HW doesn't expect kVp9ActionDone and there's a ProcessInput() already kicked during
    // TryToReschedule().
    //
    // The kRunning part doesn't seem useful (AFAICT), but also doesn't seem harmful.  We should
    // consider removing that part at some point.
    //
    // The kSwappedOut part just checks if TryToReschedule() swapped out this decoder.
    if (state_ != DecoderState::kSwappedOut && state_ != DecoderState::kRunning &&
        state_ != DecoderState::kInitialWaitingForInput) {
      DLOG("not swapped out state_: %u", static_cast<uint32_t>(state_));
      // TODO: Avoid running the decoder if there's no input data or output
      // buffers available. Once it starts running we don't let it swap out, so
      // one decoder could hang indefinitely in this case without being swapped
      // out. This can happen if the player's paused or if the client hangs.
      state_ = DecoderState::kRunning;
      DLOG("kVp9ActionDone (kRunning)");
      HevcDecStatusReg::Get().FromValue(kVp9ActionDone).WriteTo(owner_->dosbus());
      owner_->watchdog()->Start();
    }

    return;
  }

  if (dec_status != kProcessedHeader) {
    LogEvent(
        media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_DecodeResultInvalidError);
    LOG(ERROR, "Unexpected decode status %x", dec_status);
    return;
  };

  state_ = DecoderState::kPausedAtHeader;

  DLOG("PrepareNewFrame()");
  PrepareNewFrame(false);

  DLOG("Done handling VP9 interrupt");

  // PrepareNewFrame will tell the firmware to continue decoding if necessary.
}

void Vp9Decoder::ConfigureMcrcc() {
  TRACE_DURATION("media", "Vp9Decoder::ConfigureMcrcc");
  // The MCRCC seems to be used with processing reference frames.
  HevcdMcrccCtl1::Get().FromValue(0).set_reset(true).WriteTo(owner_->dosbus());
  if (current_frame_data_.keyframe || current_frame_data_.intra_only) {
    HevcdMcrccCtl1::Get().FromValue(0).set_reset(false).WriteTo(owner_->dosbus());
    return;
  }
  // Signal an autoincrementing read of some canvas table.
  HevcdMppAncCanvasAccconfigAddr::Get().FromValue(0).set_bit1(1).WriteTo(owner_->dosbus());
  // First element is probably for last frame.
  uint32_t data_addr = HevcdMppAncCanvasDataAddr::Get().ReadFrom(owner_->dosbus()).reg_value();
  data_addr &= 0xffff;
  HevcdMcrccCtl2::Get().FromValue(data_addr | (data_addr << 16)).WriteTo(owner_->dosbus());

  // Second element is probably for golden frame.
  data_addr = HevcdMppAncCanvasDataAddr::Get().ReadFrom(owner_->dosbus()).reg_value();
  data_addr &= 0xffff;
  HevcdMcrccCtl3::Get().FromValue(data_addr | (data_addr << 16)).WriteTo(owner_->dosbus());
  // Set to progressive mode.
  HevcdMcrccCtl1::Get().FromValue(0xff0).WriteTo(owner_->dosbus());
}

void Vp9Decoder::ConfigureMotionPrediction() {
  TRACE_DURATION("media", "Vp9Decoder::ConfigureMotionPrediction");
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
      .FromValue(working_buffers_->motion_prediction_above.addr32())
      .WriteTo(owner_->dosbus());

  bool last_frame_has_mv = last_frame_ && !last_frame_data_.keyframe &&
                           !last_frame_data_.intra_only &&
                           current_frame_->frame->hw_width == last_frame_->hw_width &&
                           current_frame_->frame->hw_height == last_frame_->hw_height &&
                           !current_frame_data_.error_resilient_mode && last_frame_data_.show_frame;
  HevcMpredCtrl4::Get()
      .ReadFrom(owner_->dosbus())
      .set_use_prev_frame_mvs(last_frame_has_mv)
      .WriteTo(owner_->dosbus());

  ZX_DEBUG_ASSERT(current_mpred_buffer_);
  uint32_t mv_mpred_addr = truncate_to_32(current_mpred_buffer_->phys_base());
  HevcMpredMvWrStartAddr::Get().FromValue(mv_mpred_addr).WriteTo(owner_->dosbus());
  HevcMpredMvWptr::Get().FromValue(mv_mpred_addr).WriteTo(owner_->dosbus());
  if (last_mpred_buffer_) {
    uint32_t last_mv_mpred_addr = truncate_to_32(last_mpred_buffer_->phys_base());
    HevcMpredMvRdStartAddr::Get().FromValue(last_mv_mpred_addr).WriteTo(owner_->dosbus());
    HevcMpredMvRptr::Get().FromValue(last_mv_mpred_addr).WriteTo(owner_->dosbus());

    // This is the maximum allowable size, which can be greater than the intended allocated size if
    // the size was rounded up.
    uint32_t last_end_addr = truncate_to_32(last_mv_mpred_addr + last_mpred_buffer_->size());
    HevcMpredMvRdEndAddr::Get().FromValue(last_end_addr).WriteTo(owner_->dosbus());
  }
}

void Vp9Decoder::ConfigureFrameOutput(bool bit_depth_8) {
  TRACE_DURATION("media", "Vp9Decoder::ConfigureFrameOutput");
  // SAO stands for Sample Adaptive Offset, which is a type of filtering in
  // HEVC. Sao isn't used in VP9, but the hardware that handles it also handles
  // writing frames to memory.

  HevcSaoCtrl5::Get()
      .ReadFrom(owner_->dosbus())
      .set_mode_8_bits(bit_depth_8)
      .WriteTo(owner_->dosbus());

  if (use_compressed_output_) {
    HevcdMppDecompCtl1::Get().FromValue(0).set_paged_mode(1).WriteTo(owner_->dosbus());
  } else {
    HevcdMppDecompCtl1::Get().FromValue(0).set_use_uncompressed(1).WriteTo(owner_->dosbus());
  }

  ZX_DEBUG_ASSERT(fbl::round_up(current_frame_->frame->hw_width, 2u) ==
                  current_frame_->frame->coded_width);
  ZX_DEBUG_ASSERT(fbl::round_up(current_frame_->frame->hw_height, 8u) ==
                  current_frame_->frame->coded_height);

  if (use_compressed_output_) {
    uint32_t compressed_body_size = ComputeCompressedBodySize(
        current_frame_->frame->coded_width, current_frame_->frame->coded_height, !bit_depth_8);
    uint32_t compressed_header_size = ComputeCompressedHeaderSize(
        current_frame_->frame->coded_width, current_frame_->frame->coded_height, !bit_depth_8);

    HevcdMppDecompCtl2::Get().FromValue(compressed_body_size >> 5).WriteTo(owner_->dosbus());
    HevcCmBodyLength::Get().FromValue(compressed_body_size).WriteTo(owner_->dosbus());
    // It's unclear if the header offset means anything with the MMU enabled, as
    // the header is stored separately.
    HevcCmHeaderOffset::Get().FromValue(compressed_body_size).WriteTo(owner_->dosbus());
    HevcCmHeaderLength::Get().FromValue(compressed_header_size).WriteTo(owner_->dosbus());
    HevcCmHeaderStartAddr::Get()
        .FromValue(truncate_to_32(current_frame_->compressed_header->phys_base()))
        .WriteTo(owner_->dosbus());
    assert(compressed_header_size <= current_frame_->compressed_header->size());

    uint32_t frame_buffer_size =
        fbl::round_up(compressed_body_size, static_cast<uint32_t>(PAGE_SIZE));
    if (!io_buffer_is_valid(&current_frame_->compressed_data) ||
        (io_buffer_size(&current_frame_->compressed_data, 0) != frame_buffer_size)) {
      if (io_buffer_is_valid(&current_frame_->compressed_data))
        io_buffer_release(&current_frame_->compressed_data);
      zx_status_t status = io_buffer_init(&current_frame_->compressed_data, owner_->bti()->get(),
                                          frame_buffer_size, IO_BUFFER_RW);
      if (status != ZX_OK) {
        LogEvent(
            media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_InitializationError);
        LOG(ERROR, "Couldn't allocate compressed frame data: %d", status);
        return;
      }
      SetIoBufferName(&current_frame_->compressed_data, "Vp9CompressedFrame");

      status = io_buffer_physmap(&current_frame_->compressed_data);
      if (status != ZX_OK) {
        LogEvent(
            media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_InitializationError);
        LOG(ERROR, "Couldn't map compressed frame data: %d", status);
        return;
      }
      BarrierBeforeInvalidate();
      io_buffer_cache_flush_invalidate(&current_frame_->compressed_data, 0, frame_buffer_size);
      BarrierAfterFlush();
    }

    // Enough frames for the maximum possible size of compressed video have to be
    // allocated ahead of time. The hardware will read them from
    // frame_map_mmu.buffer as needed.
    //
    // TODO(fxbug.dev/13434): Return unused frames could be returned to a pool and use
    // them for decoding a different frame.
    {
      uint32_t frame_count = frame_buffer_size / PAGE_SIZE;
      uint32_t* mmu_data =
          reinterpret_cast<uint32_t*>(working_buffers_->frame_map_mmu.buffer().virt_base());
      ZX_DEBUG_ASSERT(frame_count * 4 <= working_buffers_->frame_map_mmu.size());
      for (uint32_t i = 0; i < frame_count; i++) {
        ZX_DEBUG_ASSERT(current_frame_->compressed_data.phys_list[i] != 0);
        mmu_data[i] = static_cast<uint32_t>(current_frame_->compressed_data.phys_list[i] >> 12);
      }
      working_buffers_->frame_map_mmu.buffer().CacheFlush(0, frame_count * 4);
    }
  }

  uint32_t buffer_address = truncate_to_32(current_frame_->frame->buffer.phys_list[0]);

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
      .FromValue(current_frame_->frame->stride * current_frame_->frame->coded_height)
      .WriteTo(owner_->dosbus());
  HevcSaoCLength::Get()
      .FromValue(current_frame_->frame->stride * current_frame_->frame->coded_height / 2)
      .WriteTo(owner_->dosbus());
  // Compressed data is used as a reference for future frames, and uncompressed
  // data is output to consumers. Uncompressed data writes could be disabled in
  // the future if the consumer (e.g. the display) supported reading the
  // compressed data.
  {
    auto temp = HevcSaoCtrl1::Get().ReadFrom(owner_->dosbus());
    temp.set_mem_map_mode(HevcSaoCtrl1::kMemMapModeLinear)
        .set_endianness(HevcSaoCtrl1::kBigEndian64);
    if (use_compressed_output_) {
      if (IsDeviceAtLeast(owner_->device_type(), DeviceType::kG12A)) {
        HevcDblkCfgB::Get()
            .ReadFrom(owner_->dosbus())
            .set_compressed_write_enable(true)
            .set_uncompressed_write_enable(true)
            .WriteTo(owner_->dosbus());
      } else {
        temp.set_double_write_disable(false).set_compressed_write_disable(false);
      }
    } else {
      temp.set_double_write_disable(false).set_compressed_write_disable(true);
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
  if (have_fatal_error_)
    return false;

  // We can start decoding without output frames allocated (if other conditions are met).  This is
  // normal when starting the first stream, as output format detection requires some input data.
  if (valid_frames_count_) {
    bool has_available_output_frames = false;
    for (uint32_t i = 0; i < valid_frames_count_; i++) {
      if (frames_[i]->refcount == 0) {
        has_available_output_frames = true;
        break;
      }
    }
    if (!has_available_output_frames) {
      return false;
    }
  }

  if (!client_->IsOutputReady()) {
    return false;
  }

  return frame_data_provider_->HasMoreInputData();
}

void Vp9Decoder::ShowExistingFrame(HardwareRenderParams* params) {
  TRACE_DURATION("media", "Vp9Decoder::ShowExistingFrame");
  Frame* frame = reference_frame_map_[params->frame_to_show];
  if (!frame) {
    LogEvent(media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_MissingPictureError);
    LOG(WARNING, "Showing existing frame that doesn't exist");
    SkipFrameAfterFirmwareSlow();
    return;
  }

  // stream_offset points to an offset within the header of the frame. With
  // superframes, the offset stored in the PTS manager will be the start of the
  // superframe, but since the offset here is less than the start of the next
  // superframe the correct PTS will be found.
  //
  // When show_existing_frame is set, the original PTS from when the reference
  // frame was decoded is ignored.
  uint32_t stream_offset = HevcAssistScratchC::Get().ReadFrom(owner_->dosbus()).reg_value();

  // PtsManager does bit-extension to 64 bit stream offset.
  PtsManager::LookupResult result = pts_manager_->Lookup(stream_offset);
  DLOG("stream_offset (show existing): 0x%x has_pts: %u pts: %lu", stream_offset, result.has_pts(),
       result.pts());
  frame->frame->has_pts = result.has_pts();
  frame->frame->pts = result.pts();
  if (result.is_end_of_stream()) {
    DLOG("##### END OF STREAM DETECTED ##### (ShowExistingFrame)");
    client_->OnEos();
    return;
  }

  frame->refcount++;
  frame->client_refcount++;
  client_->OnFrameReady(frame->frame);
  ZX_DEBUG_ASSERT(state_ == DecoderState::kPausedAtHeader);
  DLOG("kVp9CommandDecodeSlice (show existing)");
  HevcDecStatusReg::Get().FromValue(kVp9CommandDecodeSlice).WriteTo(owner_->dosbus());
  state_ = DecoderState::kRunning;
  owner_->watchdog()->Start();
}

void Vp9Decoder::SkipFrameAfterFirmwareSlow() {
  TRACE_DURATION("media", "Vp9Decoder::SkipFrameAfterFirmwareSlow");
  ZX_DEBUG_ASSERT(state_ == DecoderState::kPausedAtHeader);
  // This is a fairly heavy-weight way to skip a frame (~20-40 ms), but the upside is we share more
  // code this way.
  //
  // In the long run we'll only use this method when the watchdog fires, as in that case it makes
  // sense to reset the state of the HW from scratch, and it's worth the time cost of doing so
  // (once).
  //
  // For now, for DRM streams only, we also use this method to skip frames if a client doesn't
  // provide a keyframe as the first frame of a stream (possibly for several frames until a keyframe
  // is encountered), and for several frames after the watchdog fired (again, only for DRM streams,
  // and only temporarily).
  //
  // See CodecAdapterVp9::CoreCodecResetStreamAfterCurrentFrame() for comments on how we could make
  // this faster, but we probably don't really need to.

  state_ = DecoderState::kFailed;
  frame_data_provider_->AsyncResetStreamAfterCurrentFrame();
}

void Vp9Decoder::PrepareNewFrame(bool params_checked_previously) {
  TRACE_DURATION("media", "Vp9Decoder::PrepareNewFrame");
  if (!client_->IsOutputReady()) {
    // Becomes false when ReturnFrame() gets called, at which point
    // PrepareNewFrame() gets another chance to check again and set back to true
    // as necessary.  This bool needs to exist only so that ReturnFrame() can
    // know whether the decoder is currently needing PrepareNewFrame().
    DLOG("waiting_for_output_ready_ = true");
    waiting_for_output_ready_ = true;
    return;
  }

  HardwareRenderParams params;
  // BarrierBeforeInvalidate() and BarrierAfterFlush() are handled within
  // CacheFlushInvalidate():
  working_buffers_->rpm.buffer().CacheFlushInvalidate(0, sizeof(HardwareRenderParams));
  uint16_t* input_params = reinterpret_cast<uint16_t*>(working_buffers_->rpm.buffer().virt_base());

  // Convert from middle-endian.
  for (uint32_t i = 0; i < std::size(params.data_words); i += 4) {
    for (uint32_t j = 0; j < 4; j++) {
      params.data_words[i + j] = input_params[i + (3 - j)];
    }
  }

  if (!has_keyframe_ && params.frame_type != kVp9FrameTypeKeyFrame) {
    // This path is only used by protected content that has a watchdog fire during decode or that
    // starts with a NAL that isn't a keyframe, and in any case only temporarily.
    //
    // The SkipFrameAfterFirmwareSlow() takes ~20-40 ms per frame, which isn't great.  That's why we
    // prefer to skip by parsing the cleartext frame_type from the uncompressed_header_size bytes
    // instead, which we currently do for non-DRM content.
    LogEvent(media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_SlowFrameSkip);
    LOG(WARNING, "!has_keyframe_ && params.frame_type != kVp9FrameTypeKeyFrame --- frame_type: %u",
        params.frame_type);
    SkipFrameAfterFirmwareSlow();
    return;
  }
  if (params.hw_width == 0 || params.hw_height == 0) {
    // This path exists to mitigate _potential_ problems parsing the frame header.  We've only
    // actually observed this for non-keyframe frames where we never delivered the preceding
    // keyframe to the FW, so in that case most likely the frame size information wasn't availalbe
    // to the FW.
    LogEvent(
        media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_DimensionsInvalidError);
    LOG(WARNING, "params.hw_width == 0 || params.hw_height == 0 --- hw_width: %u hw_height: %u",
        params.hw_width, params.hw_height);
    SkipFrameAfterFirmwareSlow();
    return;
  }

  // Seems like these two together are _probably_ not ever expected...(?)
  ZX_DEBUG_ASSERT(!(params.frame_type == kVp9FrameTypeKeyFrame && params.show_existing_frame));

  if (!has_keyframe_) {
    ZX_DEBUG_ASSERT(params.frame_type == kVp9FrameTypeKeyFrame);
    has_keyframe_ = true;
  }

  if (params.show_existing_frame) {
    DLOG("ShowExistingFrame()");
    ShowExistingFrame(&params);
    return;
  }

  // If this is returning false due to running out of buffers then the function will be retried once
  // more are received.
  if (!FindNewFrameBuffer(&params, params_checked_previously)) {
    return;
  }

  // We invalidate here just in case another participant is somehow creating dirty cache lines.  If
  // the participant is doing that only while the frame isn't being written to by HW, and the data
  // in the CPU cache remains equal to what's in RAM, then the harm is only the need for this
  // invalidate.  If the participant is creating such cache lines while the frame is also being
  // written by HW, then corrupted/invalid decode is possible; participants should not do that.
  //
  // Consumers should never write to frames at any time.  Frames can be used as reference frames
  // while simultaneously downstream for display, so writes to frames (of non-equal data especially)
  // can corrupt the decode of other frames.
  //
  // TODO(dustingreen): Audit sysmem initiators for attenuation of write right for consumer
  // participants that should be read-only, which may remove any need for this invalidate.  The
  // invalidate after frame decode is still necessary regardless.
  BarrierBeforeInvalidate();
  io_buffer_cache_flush_invalidate(&current_frame_->frame->buffer, 0,
                                   io_buffer_size(&current_frame_->frame->buffer, 0));
  BarrierAfterFlush();

  last_frame_data_ = current_frame_data_;
  // See comments about stream_offset above. Multiple frames will return the
  // same PTS if they're part of a superframe, but only one of the frames should
  // have show_frame set, so only that frame will be output with that PTS.
  //
  // TODO(fxbug.dev/49102): PtsManager needs to be able to help extend stream_offset from < 64 bits
  // to 64 bits.
  uint32_t stream_offset = HevcShiftByteCount::Get().ReadFrom(owner_->dosbus()).reg_value();

  // PtsManager does bit-extension to 64 bit stream offset.
  PtsManager::LookupResult result = pts_manager_->Lookup(stream_offset);
  DLOG("stream_offset (prepare new): 0x%x has_pts: %u pts: %lu", stream_offset, result.has_pts(),
       result.pts());
  current_frame_data_.has_pts = result.has_pts();
  current_frame_data_.pts = result.pts();
  if (result.is_end_of_stream()) {
    DLOG("##### END OF STREAM DETECTED ##### (PrepareNewFrame)");
    client_->OnEos();
    return;
  }

  current_frame_data_.keyframe = params.frame_type == kVp9FrameTypeKeyFrame;
  current_frame_data_.intra_only = params.intra_only;
  current_frame_data_.refresh_frame_flags = params.refresh_frame_flags;
  if (current_frame_data_.keyframe) {
    current_frame_data_.refresh_frame_flags = (1 << std::size(reference_frame_map_)) - 1;
  }
  current_frame_data_.error_resilient_mode = params.error_resilient_mode;
  current_frame_data_.show_frame = params.show_frame;

  SetRefFrames(&params);

  uint32_t hw_width = params.hw_width;
  uint32_t hw_height = params.hw_height;
  HevcParserPictureSize::Get().FromValue((hw_height << 16) | hw_width).WriteTo(owner_->dosbus());

  InitializeHardwarePictureList();
  ConfigureReferenceFrameHardware();
  ConfigureMotionPrediction();
  ConfigureMcrcc();

  ConfigureFrameOutput(params.bit_depth == 8);

  UpdateLoopFilter(&params);

  ZX_DEBUG_ASSERT(state_ == DecoderState::kPausedAtHeader);
  DLOG("kVp9CommandDecodeSlice (prepare new frame)");
  HevcDecStatusReg::Get().FromValue(kVp9CommandDecodeSlice).WriteTo(owner_->dosbus());
  state_ = DecoderState::kRunning;
  owner_->watchdog()->Start();
}

Vp9Decoder::Frame::Frame(Vp9Decoder* parent_param) : parent(parent_param) {}

Vp9Decoder::Frame::~Frame() { io_buffer_release(&compressed_data); }

bool Vp9Decoder::FindNewFrameBuffer(HardwareRenderParams* params, bool params_checked_previously) {
  TRACE_DURATION("media", "Vp9Decoder::FindNewFrameBuffer");
  ZX_ASSERT(!current_frame_);
  ZX_DEBUG_ASSERT(!waiting_for_empty_frames_);
  ZX_DEBUG_ASSERT(!waiting_for_new_frames_);

  uint32_t display_width, display_height;
  if (params->render_size_present) {
    display_width = params->render_width;
    display_height = params->render_height;
    // When there's a stream that changes dimensions from larger to smaller, the
    // HW can specify render_width, render_height that's the old size despite
    // the old size being larger than the new width, height.  In that case it
    // appears that the actual display_width and display_height are the width
    // and height.  This can still result in odd (% 2 != 0) values.
    display_width = std::min(display_width, static_cast<uint32_t>(params->hw_width));
    display_height = std::min(display_height, static_cast<uint32_t>(params->hw_height));
  } else {
    display_width = params->hw_width;
    display_height = params->hw_height;
  }

  // The Profile_0_8bit/frm_resize/crowd_run_1280X768_fr30_bd8_frm_resize_l31
  // VP9 conformance test stream covers odd width reported from HW.
  uint32_t coded_width = fbl::round_up(params->hw_width, 2u);
  // TODO(dustingreen): AFAIK, we haven't seen an odd height reported from HW
  // yet.  We may need to create a test stream to cover this.
  // Round heights to a multiple of 8, because otherwise the hardware may write
  // past the end of the Y into the UV planes.
  uint32_t coded_height = fbl::round_up(params->hw_height, 8u);
  uint32_t stride = fbl::round_up(params->hw_width, 32u);

  DLOG("coded_width: %u coded_height: %u stride: %u", coded_width, coded_height, stride);

  // Support up to 4kx2k, the hardware limit.
  constexpr uint32_t kMaxWidth = 4096, kMaxHeight = 2176;
  if (coded_width > kMaxWidth || coded_height > kMaxHeight) {
    LogEvent(media_metrics::
                 StreamProcessorEvents2MigratedMetricDimensionEvent_DimensionsUnsupportedError);
    LOG(ERROR, "Invalid stream size %dx%d", coded_width, coded_height);
    CallErrorHandler();
    return false;
  }

  bool buffers_allocated = !!frames_[0]->frame || !!frames_[0]->on_deck_frame;
  // For VP9 we have kMinFrames and kMaxFrames as the min/max bounds on # of frames the decoder is
  // able/willing to handle/track, and those constants are completely independent of any information
  // in the input stream data.  There's no reason for this decoder to ever need to check if the # of
  // buffers in the current collection is compatible with new input data, so this decoder just says
  // that the min_frame_count and max_frame_count are both the current frame count.  The current
  // collection is always ok in terms of frame count.
  if (!buffers_allocated || reallocate_buffers_next_frame_for_testing_ ||
      (!client_->IsCurrentOutputBufferCollectionUsable(valid_frames_count_, valid_frames_count_,
                                                       coded_width, coded_height, stride,
                                                       display_width, display_height))) {
    reallocate_buffers_next_frame_for_testing_ = false;
    if (params_checked_previously) {
      // If we get here, it means we're seeing rejection of
      // BufferCollectionInfo_2 settings/constraints vs. params on a thread
      // other than the interrupt handler thread which is the first thread on
      // which we learn of the incompatibilty.  This shouldn't happen.  If it
      // does happen, maybe a new BufferCollection was allocated that ended up
      // with settings/constraints that are still incompatible with what params
      // needs, which is bad enough to fail the stream.
      LogEvent(media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_UnreachableError);
      LOG(ERROR,
          "params_checked_previously - calling error_handler_, allocated %d width %d height %d",
          buffers_allocated, coded_width, coded_height);
      CallErrorHandler();
      return false;
    }
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
      // Resetting on_deck_frame should avoid leaking if dimensions change in quick succession, with
      // first buffer collection having more buffers than second.
      frames_[i]->on_deck_frame = nullptr;
      if (use_compressed_output_) {
        // In normal operation (outside decoder self-tests) this reset() is relied
        // upon to essentially signal to the CodecBuffer::frame weak_ptr<> that
        // ReturnFrame() should no longer be called on this frame.  This implies
        // (for now) that the VideoFrame must not be shared outside transients
        // under video_decoder_lock_.  See comment on Vp9Decoder::Frame::frame for
        // more.
        frames_[i]->frame = nullptr;

        // After the frames are cleared ReturnFrame can't be called on them, so we
        // need to decrement the refcounts now.
        assert(frames_[i]->refcount >= frames_[i]->client_refcount);
        frames_[i]->refcount -= frames_[i]->client_refcount;
        frames_[i]->client_refcount = 0;
      } else {
        // If the VideoFrame isn't a reference frame it will never be used again, as
        // the new on-deck frames will replace it.
        frames_[i]->ReleaseIfNonreference();
      }
    }
    valid_frames_count_ = 0;

    // VP9 doesn't have sample_aspect_ratio at ES (.ivf) layer, so here we
    // report "false, 1, 1" to indicate that the ES doesn't have a
    // sample_aspect_ratio.  The Codec client may potentially obtain
    // sample_aspect_ratio from other sources such as a .webm container. If
    // those potential sources don't provide sample_aspect_ratio, then 1:1 is
    // a reasonable default.
    zx_status_t initialize_result =
        client_->InitializeFrames(kMinFrames, kMaxFrames, coded_width, coded_height, stride,
                                  display_width, display_height, false, 1, 1);
    if (initialize_result != ZX_OK) {
      if (initialize_result != ZX_ERR_STOP) {
        LogEvent(
            media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_InitializationError);
        LOG(ERROR, "initialize_frames_handler_() failed - status: %d", initialize_result);
        CallErrorHandler();
        return false;
      }
      // EOS
      ZX_DEBUG_ASSERT(initialize_result == ZX_ERR_STOP);
      return false;
    }
    waiting_for_new_frames_ = true;
    return false;
  }

  ZX_DEBUG_ASSERT(valid_frames_count_ != 0);
  Frame* new_frame = nullptr;
  for (uint32_t i = 0; i < valid_frames_count_; i++) {
    if (frames_[i]->refcount == 0) {
      new_frame = frames_[i].get();
      break;
    }
  }
  if (!new_frame) {
    waiting_for_empty_frames_ = true;
    DLOG("Couldn't allocate framebuffer - all in use");
    return false;
  }

  if (new_frame->on_deck_frame) {
    new_frame->frame = std::move(new_frame->on_deck_frame);
    ZX_DEBUG_ASSERT(!new_frame->on_deck_frame);
  }

  // These may or may not be changing.  VP9 permits frame dimensions to change
  // from frame to frame of the same stream.  As long as the BufferCollection
  // can accommodate params (checked above), we don't need to re-allocate
  // buffers.
  new_frame->hw_width = params->hw_width;
  new_frame->hw_height = params->hw_height;
  ZX_DEBUG_ASSERT(new_frame->frame);
  new_frame->frame->hw_width = params->hw_width;
  new_frame->frame->hw_height = params->hw_height;
  new_frame->frame->coded_width = coded_width;
  new_frame->frame->coded_height = coded_height;
  new_frame->frame->stride = stride;
  new_frame->frame->display_width = display_width;
  new_frame->frame->display_height = display_height;
  // derived value
  new_frame->frame->uv_plane_offset = new_frame->frame->coded_height * new_frame->frame->stride;

  ZX_DEBUG_ASSERT(new_frame->refcount == 0);
  current_frame_ = new_frame;
  current_frame_->refcount++;
  current_frame_->decoded_index = decoded_frame_count_++;

  // get a free mpred buffer; we allocate 3 of these up-front during init to avoid needing to
  // allocate here; these are always build-time fixed size (at least for now)
  ZX_DEBUG_ASSERT(!cached_mpred_buffers_.empty());
  ZX_DEBUG_ASSERT(cached_mpred_buffers_.back());
  ZX_DEBUG_ASSERT(cached_mpred_buffers_.back()->present());
  ZX_DEBUG_ASSERT(cached_mpred_buffers_.back()->is_secure() == is_secure());
  current_mpred_buffer_ = std::move(cached_mpred_buffers_.back());
  cached_mpred_buffers_.pop_back();
  ZX_DEBUG_ASSERT(current_mpred_buffer_);
  ZX_DEBUG_ASSERT(current_mpred_buffer_->present());

  return true;
}

void Vp9Decoder::SetRefFrames(HardwareRenderParams* params) {
  TRACE_DURATION("media", "Vp9Decoder::SetRefFrames");
  for (uint32_t i = 0; i < kCurrentReferenceFrameCount; i++) {
    uint32_t ref = (params->ref_info >> (((kCurrentReferenceFrameCount - 1 - i) * 4) + 1)) & 0x7;
    assert(ref < std::size(reference_frame_map_));
    current_reference_frames_[i] = reference_frame_map_[ref];
  }
}

void Vp9Decoder::ConfigureReferenceFrameHardware() {
  TRACE_DURATION("media", "Vp9Decoder::ConfigureReferenceFrameHardware");
  // Do an autoincrementing write to one canvas table.
  HevcdMppAncCanvasAccconfigAddr::Get().FromValue(0).set_bit0(1).WriteTo(owner_->dosbus());
  for (Frame* frame : current_reference_frames_) {
    if (!frame)
      continue;

    // These are indices into the table initialized in InitializeHardwarePictureList.
    uint32_t y_index, uv_index;
    if (use_compressed_output_) {
      y_index = uv_index = frame->index;
    } else {
      y_index = frame->index * 2;
      uv_index = y_index + 1;
    }
    HevcdMppAncCanvasDataAddr::Get()
        .FromValue((uv_index << 16) | (uv_index << 8) | (y_index))
        .WriteTo(owner_->dosbus());
  }

  // Do an autoincrementing write to a different canvas table.
  HevcdMppAncCanvasAccconfigAddr::Get().FromValue(0).set_field15_8(16).set_bit0(1).WriteTo(
      owner_->dosbus());

  for (Frame* frame : current_reference_frames_) {
    if (!frame)
      continue;
    // These are indices into the table initialized in InitializeHardwarePictureList.
    uint32_t y_index, uv_index;
    if (use_compressed_output_) {
      y_index = uv_index = frame->index;
    } else {
      y_index = frame->index * 2;
      uv_index = y_index + 1;
    }
    HevcdMppAncCanvasDataAddr::Get()
        .FromValue((uv_index << 16) | (uv_index << 8) | (y_index))
        .WriteTo(owner_->dosbus());
  }

  // Do an autoincrementing write to the reference info table.
  Vp9dMppRefinfoTblAccconfig::Get().FromValue(0).set_bit2(1).WriteTo(owner_->dosbus());
  uint32_t scale_mask = 0;
  for (uint32_t i = 0; i < std::size(current_reference_frames_); i++) {
    Frame* frame = current_reference_frames_[i];
    if (!frame)
      continue;
    Vp9dMppRefinfoData::Get().FromValue(frame->hw_width).WriteTo(owner_->dosbus());
    Vp9dMppRefinfoData::Get().FromValue(frame->hw_height).WriteTo(owner_->dosbus());

    if (current_frame_->hw_width != frame->hw_width ||
        current_frame_->hw_height != frame->hw_height) {
      scale_mask |= 1 << i;
    }
    Vp9dMppRefinfoData::Get()
        .FromValue((frame->hw_width << 14) / current_frame_->hw_width)
        .WriteTo(owner_->dosbus());
    Vp9dMppRefinfoData::Get()
        .FromValue((frame->hw_height << 14) / current_frame_->hw_height)
        .WriteTo(owner_->dosbus());
    // Compressed body size. 0 If dynamically allocated
    Vp9dMppRefinfoData::Get().FromValue(0).WriteTo(owner_->dosbus());
  }

  Vp9dMppRefScaleEnable::Get().FromValue(scale_mask).WriteTo(owner_->dosbus());
}

zx_status_t Vp9Decoder::AllocateFrames() {
  TRACE_DURATION("media", "Vp9Decoder::AllocateFrames");
  for (uint32_t i = 0; i < kMaxFrames; i++) {
    auto frame = std::make_unique<Frame>(this);
    if (use_compressed_output_) {
      constexpr uint32_t kCompressedHeaderSize = 0x48000;
      constexpr uint32_t kCompressedHeaderAlignment = 1 << 16;
      constexpr bool kCompressedHeaderIsSecure = false;
      constexpr bool kCompressedHeaderIsWritable = true;
      constexpr bool kCompressedHeaderIsMappingNeeded = true;

      std::optional<InternalBuffer> compressed_header;
      ZX_DEBUG_ASSERT(!on_deck_internal_buffers_.has_value() ||
                      i >= on_deck_internal_buffers_->compressed_headers_.size() ||
                      on_deck_internal_buffers_->compressed_headers_[i].present());
      if (on_deck_internal_buffers_.has_value() &&
          i < on_deck_internal_buffers_->compressed_headers_.size()) {
        auto& on_deck = on_deck_internal_buffers_->compressed_headers_[i];
        ZX_DEBUG_ASSERT(on_deck.size() == kCompressedHeaderSize);
        ZX_DEBUG_ASSERT(on_deck.alignment() == kCompressedHeaderAlignment);
        ZX_DEBUG_ASSERT(on_deck.is_secure() == kCompressedHeaderIsSecure);
        ZX_DEBUG_ASSERT(on_deck.is_writable() == kCompressedHeaderIsWritable);
        ZX_DEBUG_ASSERT(on_deck.is_mapping_needed() == kCompressedHeaderIsMappingNeeded);
        compressed_header.emplace(std::move(on_deck));
        ZX_DEBUG_ASSERT(!on_deck_internal_buffers_->compressed_headers_[i].present());
      } else {
        auto internal_buffer = InternalBuffer::CreateAligned(
            "Vp9CompressedFrameHeader", &owner_->SysmemAllocatorSyncPtr(), owner_->bti(),
            kCompressedHeaderSize, kCompressedHeaderAlignment, kCompressedHeaderIsSecure,
            kCompressedHeaderIsWritable, kCompressedHeaderIsMappingNeeded);
        if (!internal_buffer.is_ok()) {
          LogEvent(
              media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_AllocationError);
          LOG(ERROR, "Alloc buffer error: %d", internal_buffer.error());
          return internal_buffer.error();
        }
        compressed_header.emplace(internal_buffer.take_value());
      }
      ZX_DEBUG_ASSERT(compressed_header.has_value());

      frame->compressed_header = std::move(compressed_header);
      compressed_header.reset();

      frame->compressed_header->CacheFlushInvalidate(0, kCompressedHeaderSize);
    }
    frame->index = i;
    frames_.push_back(std::move(frame));
  }

  return ZX_OK;
}

void Vp9Decoder::InitializeHardwarePictureList() {
  TRACE_DURATION("media", "Vp9Decoder::InitializeHardwarePictureList");
  // Signal autoincrementing writes to table.
  HevcdMppAnc2AxiTblConfAddr::Get().FromValue(0).set_bit1(1).set_bit2(1).WriteTo(owner_->dosbus());

  // This table maps "canvas" indices to the compressed headers of reference pictures.
  for (uint32_t i = 0; i < kMaxFrames; ++i) {
    auto& frame = frames_[i];
    std::shared_ptr<VideoFrame> video_frame = frame->frame ? frame->frame : frame->on_deck_frame;
    if (use_compressed_output_) {
      zx_paddr_t phys_addr = 0;
      if (video_frame) {
        // TODO(dustingreen): Consider a table-remap (from frames_ index to HW table index) instead
        // of using phys_addr 0.  We need to be sure the stream data can't be telling the firmware
        // to actually write to phys 0 + x.  But with old frames potentially still referenced, then
        // droppped, unclear how that'd work overall.  Or, check if HW really can be convinced to
        // write at 0 + x by using zero here.  If not, seems fine.
        phys_addr = frame->compressed_header->phys_base();
      }
      HevcdMppAnc2AxiTblData::Get()
          .FromValue(truncate_to_32(phys_addr) >> 5)
          .WriteTo(owner_->dosbus());
    } else {
      zx_paddr_t phys_addr_y = 0;
      zx_paddr_t phys_addr_uv = 0;
      if (video_frame) {
        phys_addr_y = video_frame->buffer.phys_list[0];
        phys_addr_uv = phys_addr_y + video_frame->uv_plane_offset;
      }
      // Use alternating indices for Y and UV.
      HevcdMppAnc2AxiTblData::Get()
          .FromValue(truncate_to_32(phys_addr_y) >> 5)
          .WriteTo(owner_->dosbus());
      HevcdMppAnc2AxiTblData::Get()
          .FromValue(truncate_to_32(phys_addr_uv) >> 5)
          .WriteTo(owner_->dosbus());
    }
  }

  HevcdMppAnc2AxiTblConfAddr::Get().FromValue(1).WriteTo(owner_->dosbus());

  // Set all reference picture canvas indices to 0 - do an autoincrementing
  // write.
  HevcdMppAncCanvasAccconfigAddr::Get().FromValue(0).set_bit0(1).WriteTo(owner_->dosbus());
  for (uint32_t i = 0; i < 32; ++i) {
    HevcdMppAncCanvasDataAddr::Get().FromValue(0).WriteTo(owner_->dosbus());
  }
}

void Vp9Decoder::InitializeParser() {
  TRACE_DURATION("media", "Vp9Decoder::InitializeParser");
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
  HevcCabacControl::Get().FromValue(0).set_enable(true).WriteTo(owner_->dosbus());
  HevcParserCoreControl::Get().FromValue(0).set_clock_enable(true).WriteTo(owner_->dosbus());
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

  HevcDecodeSize::Get().FromValue(0).WriteTo(owner_->dosbus());
  HevcDecodeCount::Get().FromValue(0).WriteTo(owner_->dosbus());

  HevcParserCmdWrite::Get().FromValue(1 << 16).WriteTo(owner_->dosbus());

  constexpr uint32_t parser_cmds[] = {
      0x0401, 0x8401, 0x0800, 0x0402, 0x9002, 0x1423, 0x8CC3, 0x1423, 0x8804, 0x9825,
      0x0800, 0x04FE, 0x8406, 0x8411, 0x1800, 0x8408, 0x8409, 0x8C2A, 0x9C2B, 0x1C00,
      0x840F, 0x8407, 0x8000, 0x8408, 0x2000, 0xA800, 0x8410, 0x04DE, 0x840C, 0x840D,
      0xAC00, 0xA000, 0x08C0, 0x08E0, 0xA40E, 0xFC00, 0x7C00};

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
  HevcdIppTopCntl::Get().FromValue(0).set_reset_ipp_and_mpp(true).WriteTo(owner_->dosbus());
  HevcdIppTopCntl::Get().FromValue(0).set_enable_ipp(true).WriteTo(owner_->dosbus());

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

void Vp9Decoder::OnSignaledWatchdog() {
  TRACE_DURATION("media", "Vp9Decoder::OnSignaledWatchdog");
  DLOG("Watchdog timeout");
  DLOG("HevcParserLcuStart %x", HevcParserLcuStart::Get().ReadFrom(owner_->dosbus()).reg_value());
  DLOG("HevcStreamLevel %d", HevcStreamLevel::Get().ReadFrom(owner_->dosbus()).reg_value());
  DLOG("HevcParserIntStatus 0x%x",
       HevcParserIntStatus::Get().ReadFrom(owner_->dosbus()).reg_value());
  LogEvent(media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_WatchdogFired);
  if (!frame_data_provider_) {
    LOG(ERROR, "Got Vp9 watchdog timeout - fatal error");
    CallErrorHandler();
    return;
  }
  LogEvent(media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent_StreamReset);
  LOG(ERROR, "Got Vp9 watchdog timeout.  Doing async reset of the stream after current frame.");
  state_ = DecoderState::kFailed;
  frame_data_provider_->AsyncResetStreamAfterCurrentFrame();
}

zx_status_t Vp9Decoder::SetupProtection() {
  return owner_->SetProtected(VideoDecoder::Owner::ProtectableHardwareUnit::kHevc, is_secure());
}

Vp9Decoder::InternalBuffers Vp9Decoder::TakeInternalBuffers() {
  InternalBuffers result;

  // We extract any in-use, cached, or on_deck buffers that we can find, but only if the
  // InternalBuffer is fully present().  Some of the checking for present() isn't presently needed
  // and could be asserted instead, but it's more obviously correct to check everything here.

  //
  // working buffers
  //

  if (working_buffers_.has_value()) {
    result.working_buffers_ = std::move(working_buffers_);
    working_buffers_.reset();
  } else if (on_deck_internal_buffers_.has_value() &&
             on_deck_internal_buffers_->working_buffers_.has_value() &&
             on_deck_internal_buffers_->working_buffers_->rpm.has_buffer() &&
             on_deck_internal_buffers_->working_buffers_->rpm.buffer().present()) {
    result.working_buffers_ = std::move(on_deck_internal_buffers_->working_buffers_);
    on_deck_internal_buffers_->working_buffers_.reset();
  }

  //
  // mpred buffers
  //

  if (current_mpred_buffer_ && current_mpred_buffer_->present()) {
    result.mpred_buffers_.emplace_back(std::move(*current_mpred_buffer_));
    current_mpred_buffer_.reset();
  }

  if (last_mpred_buffer_ && last_mpred_buffer_->present()) {
    result.mpred_buffers_.emplace_back(std::move(*last_mpred_buffer_));
    last_mpred_buffer_.reset();
  }

  for (auto& mpred_buffer : cached_mpred_buffers_) {
    ZX_DEBUG_ASSERT(mpred_buffer);
    ZX_DEBUG_ASSERT(mpred_buffer->present());
    if (!mpred_buffer || !mpred_buffer->present()) {
      continue;
    }
    result.mpred_buffers_.emplace_back(std::move(*mpred_buffer));
    ZX_DEBUG_ASSERT(!mpred_buffer->present());
  }
  cached_mpred_buffers_.clear();

  // We eagerly move into cached_mpred_buffers_ during GiveInternalBuffers() so we'll never see
  // non-empty here.
  ZX_ASSERT(!on_deck_internal_buffers_.has_value() ||
            on_deck_internal_buffers_->mpred_buffers_.empty());

  //
  // compressed headers
  //

  for (auto& frame_ptr : frames_) {
    auto& frame = *frame_ptr;
    if (!frame.compressed_header.has_value() || !frame.compressed_header.value().present()) {
      continue;
    }
    result.compressed_headers_.emplace_back(std::move(frame.compressed_header.value()));
    frame.compressed_header.reset();
  }
  if (on_deck_internal_buffers_.has_value()) {
    for (auto& on_deck_compressed_header : on_deck_internal_buffers_->compressed_headers_) {
      if (!on_deck_compressed_header.present()) {
        continue;
      }
      result.compressed_headers_.emplace_back(std::move(on_deck_compressed_header));
      ZX_DEBUG_ASSERT(!on_deck_compressed_header.present());
    }
    on_deck_internal_buffers_->compressed_headers_.clear();
  }

  on_deck_internal_buffers_.reset();

  return result;
}

void Vp9Decoder::GiveInternalBuffers(Vp9Decoder::InternalBuffers internal_buffers) {
  on_deck_internal_buffers_.emplace(std::move(internal_buffers));
  //  on_deck_internal_buffers_->working_buffers_->Zero();

  // pre-move into cached_mpred_buffers_ to avoid needing to check two places when we need a free
  // mpred buffer, and avoid needing to create an InternalBuffers early just to have a place to
  // stash cached mpred buffers
  for (auto& mpred_buffer : on_deck_internal_buffers_->mpred_buffers_) {
    cached_mpred_buffers_.emplace_back(std::make_unique<InternalBuffer>(std::move(mpred_buffer)));
    ZX_DEBUG_ASSERT(cached_mpred_buffers_.back() && cached_mpred_buffers_.back()->present());
  }
  on_deck_internal_buffers_->mpred_buffers_.clear();
}

}  // namespace amlogic_decoder
