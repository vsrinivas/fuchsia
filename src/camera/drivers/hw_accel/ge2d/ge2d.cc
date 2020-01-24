// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ge2d.h"

#include <lib/image-format/image_format.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <ddktl/protocol/amlogiccanvas.h>
#include <fbl/auto_lock.h>
#include <hw/reg.h>

#include "src/camera/drivers/hw_accel/ge2d/ge2d-regs.h"
#include "src/lib/syslog/cpp/logger.h"

namespace ge2d {

namespace {

constexpr uint32_t kGe2d = 0;
constexpr auto kTag = "ge2d";

enum {
  COMPONENT_PDEV,
  COMPONENT_SENSOR,
  COMPONENT_CANVAS,
  COMPONENT_COUNT,
};

}  // namespace

zx_status_t Ge2dDevice::Ge2dInitTaskResize(
    const buffer_collection_info_2_t* input_buffer_collection,
    const buffer_collection_info_2_t* output_buffer_collection, const resize_info_t* info,
    const image_format_2_t* input_image_format,
    const image_format_2_t* output_image_format_table_list, size_t output_image_format_table_count,
    uint32_t output_image_format_index, const hw_accel_frame_callback_t* frame_callback,
    const hw_accel_res_change_callback_t* res_callback,
    const hw_accel_remove_task_callback_t* task_remove_callback, uint32_t* out_task_index) {
  if (out_task_index == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto task = std::make_unique<Ge2dTask>();
  zx_status_t status = task->InitResize(
      input_buffer_collection, output_buffer_collection, info, input_image_format,
      output_image_format_table_list, output_image_format_table_count, output_image_format_index,
      frame_callback, res_callback, task_remove_callback, bti_, canvas_);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Task Creation Failed";
    return status;
  }

  // Put an entry in the hashmap.
  task_map_[next_task_index_] = std::move(task);
  *out_task_index = next_task_index_;
  next_task_index_++;

  return status;
}

zx_status_t Ge2dDevice::Ge2dInitTaskWaterMark(
    const buffer_collection_info_2_t* input_buffer_collection,
    const buffer_collection_info_2_t* output_buffer_collection, const water_mark_info_t* info,
    zx::vmo watermark_vmo, const image_format_2_t* image_format_table_list,
    size_t image_format_table_count, uint32_t image_format_index,
    const hw_accel_frame_callback_t* frame_callback,
    const hw_accel_res_change_callback_t* res_callback,
    const hw_accel_remove_task_callback_t* task_remove_callback, uint32_t* out_task_index) {
  if (out_task_index == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto task = std::make_unique<Ge2dTask>();
  zx_status_t status =
      task->InitWatermark(input_buffer_collection, output_buffer_collection, info, watermark_vmo,
                          image_format_table_list, image_format_table_count, image_format_index,
                          frame_callback, res_callback, task_remove_callback, bti_, canvas_);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Task Creation Failed";
    return status;
  }

  // Put an entry in the hashmap.
  task_map_[next_task_index_] = std::move(task);
  *out_task_index = next_task_index_;
  next_task_index_++;

  return status;
}

void Ge2dDevice::Ge2dRemoveTask(uint32_t task_index) {
  // Find the entry in hashmap.
  auto task_entry = task_map_.find(task_index);
  ZX_ASSERT(task_entry != task_map_.end());

  // Remove map entry.
  task_map_.erase(task_entry);
}

void Ge2dDevice::Ge2dReleaseFrame(uint32_t task_index, uint32_t buffer_index) {
  // Find the entry in hashmap.
  auto task_entry = task_map_.find(task_index);
  ZX_ASSERT(task_entry != task_map_.end());

  auto task = task_entry->second.get();
  ZX_ASSERT(ZX_OK == task->ReleaseOutputBuffer(buffer_index));
}

zx_status_t Ge2dDevice::Ge2dSetOutputResolution(uint32_t task_index,
                                                uint32_t new_output_image_format_index) {
  // Find the entry in hashmap.
  auto task_entry = task_map_.find(task_index);
  if (task_entry == task_map_.end()) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (task_entry->second->Ge2dTaskType() != Ge2dTask::GE2D_RESIZE) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Validate new image format index|.
  if (!task_entry->second->IsOutputFormatIndexValid(new_output_image_format_index)) {
    return ZX_ERR_INVALID_ARGS;
  }

  TaskInfo info;
  info.op = GE2D_OP_SETOUTPUTRES;
  info.task = task_entry->second.get();
  info.index = new_output_image_format_index;

  // Put the task on queue.
  fbl::AutoLock lock(&lock_);
  processing_queue_.push_front(info);
  frame_processing_signal_.Signal();
  return ZX_OK;
}

zx_status_t Ge2dDevice::Ge2dSetInputAndOutputResolution(uint32_t task_index,
                                                        uint32_t new_image_format_index) {
  // Find the entry in hashmap.
  auto task_entry = task_map_.find(task_index);
  if (task_entry == task_map_.end()) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (task_entry->second->Ge2dTaskType() != Ge2dTask::GE2D_WATERMARK) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Validate new image format index|.
  if (!task_entry->second->IsInputFormatIndexValid(new_image_format_index) ||
      !task_entry->second->IsOutputFormatIndexValid(new_image_format_index)) {
    return ZX_ERR_INVALID_ARGS;
  }

  TaskInfo info;
  info.op = GE2D_OP_SETINPUTOUTPUTRES;
  info.task = task_entry->second.get();
  info.index = new_image_format_index;

  // Put the task on queue.
  fbl::AutoLock lock(&lock_);
  processing_queue_.push_front(info);
  frame_processing_signal_.Signal();
  return ZX_OK;
}

zx_status_t Ge2dDevice::Ge2dProcessFrame(uint32_t task_index, uint32_t input_buffer_index) {
  // Find the entry in hashmap.
  auto task_entry = task_map_.find(task_index);
  if (task_entry == task_map_.end()) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Validate |input_buffer_index|.
  if (!task_entry->second->IsInputBufferIndexValid(input_buffer_index)) {
    return ZX_ERR_INVALID_ARGS;
  }

  TaskInfo info;
  info.op = GE2D_OP_FRAME;
  info.task = task_entry->second.get();
  info.index = input_buffer_index;

  // Put the task on queue.
  fbl::AutoLock lock(&lock_);
  processing_queue_.push_front(info);
  frame_processing_signal_.Signal();
  return ZX_OK;
}

void Ge2dDevice::Ge2dSetCropRect(uint32_t task_index, const rect_t* crop) {}

void Ge2dDevice::InitializeScalingCoefficients() {
  // 33x4 FIR coefficients to use. First takes 100% of pixel[1], while the last takes 50% of
  // pixel[1] and pixel[2].
  constexpr uint32_t kBilinearCoefficients[] = {
      0x00800000, 0x007e0200, 0x007c0400, 0x007a0600, 0x00780800, 0x00760a00, 0x00740c00,
      0x00720e00, 0x00701000, 0x006e1200, 0x006c1400, 0x006a1600, 0x00681800, 0x00661a00,
      0x00641c00, 0x00621e00, 0x00602000, 0x005e2200, 0x005c2400, 0x005a2600, 0x00582800,
      0x00562a00, 0x00542c00, 0x00522e00, 0x00503000, 0x004e3200, 0x004c3400, 0x004a3600,
      0x00483800, 0x00463a00, 0x00443c00, 0x00423e00, 0x00404000};

  // Vertical scaler autoincrementing write
  ScaleCoefIdx::Get().FromValue(0).WriteTo(&ge2d_mmio_);
  for (uint32_t value : kBilinearCoefficients) {
    ScaleCoef::Get().FromValue(value).WriteTo(&ge2d_mmio_);
  }
  // Horizontal scaler autoincrementing write
  ScaleCoefIdx::Get().FromValue(0).set_horizontal(1).WriteTo(&ge2d_mmio_);
  for (uint32_t value : kBilinearCoefficients) {
    ScaleCoef::Get().FromValue(value).WriteTo(&ge2d_mmio_);
  }
}

void Ge2dDevice::ProcessTask(TaskInfo& info) {
  switch (info.op) {
    case GE2D_OP_SETOUTPUTRES:
    case GE2D_OP_SETINPUTOUTPUTRES:
      return ProcessChangeResolution(info);
    case GE2D_OP_FRAME:
      return ProcessFrame(info);
  }
}

void Ge2dDevice::ProcessChangeResolution(TaskInfo& info) {
  auto task = info.task;

  // This has to free and reallocate the output buffer canvas ids.
  task->Ge2dChangeOutputRes(info.index);
  if (info.op == GE2D_OP_SETINPUTOUTPUTRES) {
    // This has to free and reallocate the input buffer canvas ids.
    task->Ge2dChangeInputRes(info.index);
  }
  frame_available_info f_info;
  f_info.frame_status = FRAME_STATUS_OK;
  f_info.metadata.timestamp = static_cast<uint64_t>(zx_clock_get_monotonic());
  f_info.metadata.image_format_index = task->output_format_index();
  return task->ResolutionChangeCallback(&f_info);
}

// Floors.
static uint32_t ConvertToFixedPoint24(double input) {
  return static_cast<uint32_t>((1 << 24) * input);
}

static void CalculateInitialPhase(uint32_t input_dim, uint32_t output_dim, uint32_t* phase_out,
                                  uint32_t* repeat_out) {
  // Linux uses a multiplied-by-10 fixed-point, but this seems simpler and more precise.
  double rate_ratio = static_cast<double>(output_dim) / input_dim;
  if (rate_ratio == 1.0) {
    *phase_out = 0;
    *repeat_out = 0;
  } else {
    // We subtract 0.5 here because the pixel value itself is at phase 0, not 0.5.
    double pixel_initial_phase = 0.5 / rate_ratio - 0.5;
    // We need to decide how to fill in the FIR filter initially.
    if (pixel_initial_phase >= 0) {
      // When scaling down the first output pixel center is after the first input pixel center, so
      // we set repeat = 1 so the inputs looks like (image[0], image[0], image[1], image[2]) and we
      // interpolate between image[0] and image[1].
      *repeat_out = 1;
    } else {
      // When scaling up the first output pixel center is before the first input pixel center, so we
      // set repeat = 2 and the input looks like (image[0], image[0], image[0], image[1]) so the
      // first output must be image[0] (due to the bilinear filter coefficients we're using).
      *repeat_out = 2;
      // Increase initial phase by 1 to compensate.
      pixel_initial_phase++;
    }
    *phase_out = ConvertToFixedPoint24(pixel_initial_phase);
  }
}

void Ge2dDevice::InitializeScaler(uint32_t input_width, uint32_t input_height,
                                  uint32_t output_width, uint32_t output_height) {
  bool horizontal_scaling = (input_width != output_width);
  bool vertical_scaling = (input_height != output_height);
  InitializeScalingCoefficients();
  bool use_preh_scaler = input_width > output_width * 2;
  bool use_prev_scaler = input_height > output_height * 2;

  // Prescaler seems to divide size by 2.
  uint32_t scaler_input_width = use_preh_scaler ? ((input_width + 1) / 2) : input_width;
  uint32_t scaler_input_height = use_prev_scaler ? ((input_height + 1) / 2) : input_height;

  // The scaler starts at an initial phase value, and for every output pixel increments it by a
  // step.  Integer values (in 5.24 fixed-point) are the input pixel values themselves (starting at
  // 0). The scaler is a polyphase scaler, so the phase picks the FIR coefficients to use (from the
  // table above). For bilinear filtering, a phase of 0 takes all its input from pixel[1], and 1
  // would take it all from pixel[2].

  constexpr uint32_t kFixedPoint = 24;
  uint32_t hsc_phase_step =
      ConvertToFixedPoint24(static_cast<double>(scaler_input_width) / output_width);
  uint32_t vsc_phase_step =
      ConvertToFixedPoint24(static_cast<double>(scaler_input_height) / output_height);

  // Horizontal scaler dividing provides more efficiency (somehow). It seems like it allows
  // calculating phases at larger blocks.
  // The dividing length is roughly 124 * (output_width / input_width).
  uint32_t hsc_dividing_length = ConvertToFixedPoint24(124) / hsc_phase_step;
  uint32_t hsc_rounded_step = hsc_dividing_length * hsc_phase_step;
  uint32_t hsc_advance_num = hsc_rounded_step >> kFixedPoint;
  uint32_t hsc_advance_phase = hsc_rounded_step & ((1 << kFixedPoint) - 1);

  uint32_t horizontal_initial_phase, horizontal_repeat;
  uint32_t vertical_initial_phase, vertical_repeat;
  // The linux driver uses |input_width| and |input_height| here, but that seems incorrect.
  CalculateInitialPhase(scaler_input_width, output_width, &horizontal_initial_phase,
                        &horizontal_repeat);
  CalculateInitialPhase(scaler_input_height, output_height, &vertical_initial_phase,
                        &vertical_repeat);

  ScMiscCtrl::Get()
      .ReadFrom(&ge2d_mmio_)
      .set_hsc_div_en(horizontal_scaling)
      .set_hsc_dividing_length(hsc_dividing_length)
      .set_pre_hsc_enable(use_preh_scaler)
      .set_pre_vsc_enable(use_prev_scaler)
      .set_vsc_enable(vertical_scaling)
      .set_hsc_enable(horizontal_scaling)
      .set_hsc_rpt_ctrl(1)
      .set_vsc_rpt_ctrl(1)
      .WriteTo(&ge2d_mmio_);

  HscStartPhaseStep::Get().FromValue(0).set_phase_step(hsc_phase_step).WriteTo(&ge2d_mmio_);
  HscAdvCtrl::Get()
      .FromValue(0)
      .set_advance_num(hsc_advance_num & 0xff)
      .set_advance_phase(hsc_advance_phase)
      .WriteTo(&ge2d_mmio_);

  // We clamp the initial phases, because that's what the hardware
  // supports. This can mess up scaling down to <= 1/3, though the prescaler can
  // help reduce how often that's a problem. The linux driver wraps these
  // values, which seems worse.
  HscIniCtrl::Get()
      .FromValue(0)
      .set_horizontal_repeat_p0(horizontal_repeat)
      .set_horizontal_advance_num_upper(hsc_advance_num >> 8)
      .set_horizontal_initial_phase(std::min(horizontal_initial_phase, 0xffffffu))
      .WriteTo(&ge2d_mmio_);

  VscStartPhaseStep::Get().FromValue(0).set_phase_step(vsc_phase_step).WriteTo(&ge2d_mmio_);
  VscIniCtrl::Get()
      .FromValue(0)
      .set_vertical_repeat_p0(vertical_repeat)
      .set_vertical_initial_phase(std::min(vertical_initial_phase, 0xffffffu))
      .WriteTo(&ge2d_mmio_);
  // Leave horizontal and vertical phase slopes set to 0.
}

void Ge2dDevice::ProcessFrame(TaskInfo& info) {
  auto task = info.task;

  auto input_buffer_index = info.index;

  auto output_buffer = task->WriteLockOutputBuffer();
  if (!output_buffer) {
    frame_available_info f_info;
    f_info.frame_status = FRAME_STATUS_ERROR_FRAME;
    f_info.buffer_id = 0;
    f_info.metadata.timestamp = static_cast<uint64_t>(zx_clock_get_monotonic());
    f_info.metadata.image_format_index = task->output_format_index();
    f_info.metadata.input_buffer_index = input_buffer_index;
    task->FrameReadyCallback(&f_info);
    return;
  }

  image_format_2_t input_format = task->input_format();

  image_format_2_t output_format = task->output_format();
  uint32_t output_x_end = output_format.coded_width - 1;
  uint32_t output_y_end = output_format.coded_height - 1;

  resize_info_t resize_info;
  if (task->Ge2dTaskType() == Ge2dTask::GE2D_RESIZE) {
    resize_info = task->resize_info();
  } else {
    resize_info.crop.x = 0;
    resize_info.crop.y = 0;
    resize_info.crop.width = input_format.coded_width;
    resize_info.crop.height = input_format.coded_height;
  }

  bool scaling_enabled = (resize_info.crop.width != output_format.coded_width) ||
                         (resize_info.crop.height != output_format.coded_height);

  uint32_t input_x_start = resize_info.crop.x;
  uint32_t input_x_end = resize_info.crop.x + resize_info.crop.width - 1;
  uint32_t input_y_start = resize_info.crop.y;
  uint32_t input_y_end = resize_info.crop.y + resize_info.crop.height - 1;

  InitializeScaler(resize_info.crop.width, resize_info.crop.height, output_format.coded_width,
                   output_format.coded_height);

  // Assume NV12 input and output for now. DST1 gets Y and DST2 gets CbCr.
  GenCtrl0::Get()
      .FromValue(0)
      .set_src1_separate_enable(true)
      .set_x_yc_ratio(1)
      .set_y_yc_ratio(1)
      .WriteTo(&ge2d_mmio_);
  GenCtrl2::Get()
      .FromValue(0)
      .set_dst_little_endian(0)  // endianness conversion happens in canvas
      .set_dst1_color_map(0)
      .set_dst1_format(GenCtrl2::kFormat8Bit)
      .set_src1_little_endian(0)  // endianness conversion happens in canvas
      .set_src1_color_map(GenCtrl2::kColorMap24NV12)
      .set_src1_format(GenCtrl2::kFormat24Bit)
      .set_src1_color_expand_mode(1)
      .WriteTo(&ge2d_mmio_);

  Src1ClipXStartEnd::Get()
      .FromValue(0)
      .set_end(input_x_end)
      .set_start(input_x_start)
      .WriteTo(&ge2d_mmio_);
  // The linux driver does Src1XStartEnd.set_start_extra(2).set_end_extra(3) but that seems to cause
  // the first columns's chroma to be duplicated.
  Src1XStartEnd::Get()
      .FromValue(0)
      .set_end(input_x_end)
      .set_start(input_x_start)
      .WriteTo(&ge2d_mmio_);
  Src1ClipYStartEnd::Get()
      .FromValue(0)
      .set_end(input_y_end)
      .set_start(input_y_start)
      .WriteTo(&ge2d_mmio_);
  // The linux driver does Src1YStartEnd.set_start_extra(2) but that seems to cause the first row's
  // chroma to be duplicated.
  Src1YStartEnd::Get()
      .FromValue(0)
      .set_end(input_y_end)
      .set_start(input_y_start)
      .set_end_extra(3)
      .WriteTo(&ge2d_mmio_);
  DstClipXStartEnd::Get().FromValue(0).set_end(output_x_end).set_start(0).WriteTo(&ge2d_mmio_);
  DstXStartEnd::Get().FromValue(0).set_end(output_x_end).set_start(0).WriteTo(&ge2d_mmio_);
  DstClipYStartEnd::Get().FromValue(0).set_end(output_y_end).set_start(0).WriteTo(&ge2d_mmio_);
  DstYStartEnd::Get().FromValue(0).set_end(output_y_end).set_start(0).WriteTo(&ge2d_mmio_);
  GenCtrl3::Get()
      .FromValue(0)
      .set_dst2_color_map(GenCtrl2::kColorMap16CbCr)
      .set_dst2_format(GenCtrl2::kFormat16Bit)
      .set_dst2_x_discard_mode(GenCtrl3::kDiscardModeOdd)
      .set_dst2_y_discard_mode(GenCtrl3::kDiscardModeOdd)
      .set_dst2_enable(1)
      .set_dst1_enable(1)
      .WriteTo(&ge2d_mmio_);
  auto& input_ids = task->GetInputCanvasIds(input_buffer_index);
  Src1Canvas::Get()
      .FromValue(0)
      .set_y(input_ids.canvas_idx[kYComponent].id())
      .set_u(input_ids.canvas_idx[kUVComponent].id())
      .set_v(input_ids.canvas_idx[kUVComponent].id())
      .WriteTo(&ge2d_mmio_);
  auto& output_canvas = task->GetOutputCanvasIds(output_buffer->vmo_handle());
  Src2DstCanvas::Get()
      .FromValue(0)
      .set_dst1(output_canvas.canvas_idx[kYComponent].id())
      .set_dst2(output_canvas.canvas_idx[kUVComponent].id())
      .WriteTo(&ge2d_mmio_);

  // To match the linux driver we repeat the UV planes instead of interpolating if we're not
  // scaling the output. This is arguably incorrect, depending on chroma siting.
  Src1FmtCtrl::Get()
      .FromValue(0)
      .set_horizontal_enable(true)
      .set_vertical_enable(true)
      .set_y_chroma_phase(0x4c)
      .set_x_chroma_phase(0x8)
      .set_horizontal_repeat(!scaling_enabled)
      .set_vertical_repeat(!scaling_enabled)
      .WriteTo(&ge2d_mmio_);
  CmdCtrl::Get().FromValue(0).set_cmd_wr(1).WriteTo(&ge2d_mmio_);

  zx_port_packet_t packet;
  ZX_ASSERT(ZX_OK == WaitForInterrupt(&packet));
  if (packet.key == kPortKeyIrqMsg) {
    ZX_ASSERT(ge2d_irq_.ack() == ZX_OK);
  }

  ZX_ASSERT(!Status0::Get().ReadFrom(&ge2d_mmio_).busy());

  if (packet.key == kPortKeyDebugFakeInterrupt || packet.key == kPortKeyIrqMsg) {
    // Invoke the callback function and tell about the output buffer index
    // which is ready to be used.
    frame_available_info f_info;
    f_info.frame_status = FRAME_STATUS_OK;
    f_info.buffer_id = output_buffer->ReleaseWriteLockAndGetIndex();
    f_info.metadata.timestamp = static_cast<uint64_t>(zx_clock_get_monotonic());
    f_info.metadata.image_format_index = task->output_format_index();
    f_info.metadata.input_buffer_index = input_buffer_index;
    task->FrameReadyCallback(&f_info);
  }
}

int Ge2dDevice::FrameProcessingThread() {
  FX_LOG(INFO, kTag, "start");
  for (;;) {
    fbl::AutoLock al(&lock_);
    while (processing_queue_.empty() && !shutdown_) {
      frame_processing_signal_.Wait(&lock_);
    }
    if (shutdown_) {
      break;
    }
    auto info = processing_queue_.back();
    processing_queue_.pop_back();
    al.release();
    ProcessTask(info);
  }
  return ZX_OK;
}

zx_status_t Ge2dDevice::StartThread() {
  return thrd_status_to_zx_status(thrd_create_with_name(
      &processing_thread_,
      [](void* arg) -> int { return reinterpret_cast<Ge2dDevice*>(arg)->FrameProcessingThread(); },
      this, "ge2d-processing-thread"));
}

zx_status_t Ge2dDevice::StopThread() {
  // Signal the worker thread and wait for it to terminate.
  {
    fbl::AutoLock al(&lock_);
    shutdown_ = true;
    frame_processing_signal_.Signal();
  }
  JoinThread();
  return ZX_OK;
}

zx_status_t Ge2dDevice::WaitForInterrupt(zx_port_packet_t* packet) {
  return port_.wait(zx::time::infinite(), packet);
}

// static
zx_status_t Ge2dDevice::Setup(zx_device_t* parent, std::unique_ptr<Ge2dDevice>* out) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    FX_LOG(ERROR, kTag, "could not get composite protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_device_t* components[COMPONENT_COUNT];
  size_t actual;
  composite.GetComponents(components, COMPONENT_COUNT, &actual);
  if (actual != COMPONENT_COUNT) {
    FX_LOG(ERROR, kTag, "Could not get components");
    return ZX_ERR_NOT_SUPPORTED;
  }

  ddk::PDev pdev(components[COMPONENT_PDEV]);
  if (!pdev.is_valid()) {
    FX_LOG(ERROR, kTag, "ZX_PROTOCOL_PDEV not available");
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> ge2d_mmio;
  zx_status_t status = pdev.MapMmio(kGe2d, &ge2d_mmio);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "pdev_.MapMmio failed";
    return status;
  }

  zx::interrupt ge2d_irq;
  status = pdev.GetInterrupt(0, &ge2d_irq);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "pdev_.GetInterrupt failed";
    return status;
  }

  zx::port port;
  status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "port create failed";
    return status;
  }

  status = ge2d_irq.bind(port, kPortKeyIrqMsg, 0 /*options*/);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "interrupt bind failed";
    return status;
  }

  zx::bti bti;
  status = pdev.GetBti(0, &bti);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "could not obtain bti";
    return status;
  }

  ddk::AmlogicCanvasProtocolClient canvas(components[COMPONENT_CANVAS]);
  if (!canvas.is_valid()) {
    FX_LOG(ERROR, kTag, "Could not get Amlogic Canvas protocol");
    return ZX_ERR_NO_RESOURCES;
  }
  amlogic_canvas_protocol_t c;
  canvas.GetProto(&c);

  // TODO(fxb/43822): Initialize clock.
  GenCtrl1::Get().FromValue(0).set_soft_reset(1).WriteTo(&*ge2d_mmio);
  GenCtrl1::Get().FromValue(0).set_soft_reset(0).WriteTo(&*ge2d_mmio);
  GenCtrl1::Get().FromValue(0).set_interrupt_control(1).WriteTo(&*ge2d_mmio);

  auto ge2d_device = std::make_unique<Ge2dDevice>(
      parent, std::move(*ge2d_mmio), std::move(ge2d_irq), std::move(bti), std::move(port), c);

  status = ge2d_device->StartThread();
  *out = std::move(ge2d_device);
  return status;
}

void Ge2dDevice::DdkUnbindNew(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void Ge2dDevice::DdkRelease() {
  StopThread();
  delete this;
}

void Ge2dDevice::ShutDown() {}

}  // namespace ge2d
