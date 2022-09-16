// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/pipe-manager.h"

#include <lib/mmio/mmio-buffer.h>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"
#include "src/graphics/display/drivers/intel-i915-tgl/intel-i915-tgl.h"
#include "src/graphics/display/drivers/intel-i915-tgl/pipe.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-pipe.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-transcoder.h"

namespace i915_tgl {

PipeManager::PipeManager(std::vector<std::unique_ptr<Pipe>> pipes) : pipes_(std::move(pipes)) {}

Pipe* PipeManager::RequestPipe(DisplayDevice& display) {
  Pipe* new_pipe = GetAvailablePipe();
  if (new_pipe) {
    new_pipe->AttachToDisplay(display.id(), display.type() == DisplayDevice::Type::kEdp);
    pipes_reallocated_ = true;
  }
  return new_pipe;
}

Pipe* PipeManager::RequestPipeFromHardwareState(DisplayDevice& display,
                                                fdf::MmioBuffer* mmio_space) {
  Pipe* hw_state_pipe = GetPipeFromHwState(display.ddi(), mmio_space);
  if (hw_state_pipe) {
    hw_state_pipe->AttachToDisplay(display.id(), display.type() == DisplayDevice::Type::kEdp);
    pipes_reallocated_ = true;
  }
  return hw_state_pipe;
}

void PipeManager::ReturnPipe(Pipe* pipe) {
  bool has_pipe =
      std::any_of(pipes_.begin(), pipes_.end(), [pipe](const auto& p) { return p.get() == pipe; });
  ZX_DEBUG_ASSERT(has_pipe && pipe->in_use());

  pipe->Reset();
  pipe->ResetActiveTranscoder();
  pipe->Detach();
  pipes_reallocated_ = true;
}

bool PipeManager::PipeReallocated() {
  bool result = pipes_reallocated_;
  pipes_reallocated_ = false;
  return result;
}

Pipe* PipeManager::operator[](tgl_registers::Pipe idx) const {
  ZX_DEBUG_ASSERT(idx < pipes_.size());
  return idx < pipes_.size() ? pipes_.at(idx).get() : nullptr;
}

Pipe* PipeManager::At(tgl_registers::Pipe idx) const {
  return idx < pipes_.size() ? pipes_.at(idx).get() : nullptr;
}

PipeManager::PipeIterator PipeManager::begin() { return PipeIterator(pipes_.begin()); }
PipeManager::PipeIterator PipeManager::end() { return PipeIterator(pipes_.end()); }
PipeManager::PipeConstIterator PipeManager::begin() const {
  return PipeConstIterator(pipes_.cbegin());
}
PipeManager::PipeConstIterator PipeManager::end() const { return PipeConstIterator(pipes_.cend()); }

PipeManagerSkylake::PipeManagerSkylake(Controller* controller)
    : PipeManager(GetPipes(controller->mmio_space(), controller->power())),
      mmio_space_(controller->mmio_space()) {
  ZX_DEBUG_ASSERT(controller);
}

void PipeManagerSkylake::ResetInactiveTranscoders() {
  bool has_edp = false;
  for (Pipe* pipe : *this) {
    if (pipe->in_use()) {
      has_edp |= pipe->transcoder() == tgl_registers::TRANS_EDP;
      if (pipe->transcoder() == tgl_registers::TRANS_EDP) {
        tgl_registers::Trans unused_trans = static_cast<tgl_registers::Trans>(pipe->pipe());
        Pipe::ResetTrans(unused_trans, mmio_space_);
        zxlogf(DEBUG, "Reset unused transcoder %d for pipe %d (trans inactive)", unused_trans,
               pipe->pipe());
      }
    } else {
      Pipe::ResetTrans(pipe->transcoder(), mmio_space_);
      zxlogf(DEBUG, "Reset unused transcoder %d for pipe %d (pipe inactive)", pipe->transcoder(),
             pipe->pipe());
    }
  }

  if (!has_edp) {
    Pipe::ResetTrans(tgl_registers::TRANS_EDP, mmio_space_);
    zxlogf(DEBUG, "Reset unused transcoder TRANS_EDP (not used by any pipe)");
  }
}

Pipe* PipeManagerSkylake::GetAvailablePipe() {
  for (Pipe* pipe : *this) {
    if (!pipe->in_use()) {
      return pipe;
    }
  }
  return nullptr;
}

Pipe* PipeManagerSkylake::GetPipeFromHwState(tgl_registers::Ddi ddi, fdf::MmioBuffer* mmio_space) {
  // In Skylake, DDI_A always maps to eDP display.
  if (ddi == tgl_registers::DDI_A) {
    tgl_registers::TranscoderRegs regs(tgl_registers::TRANS_EDP);
    auto ddi_func_ctrl = regs.DdiFuncControl().ReadFrom(mmio_space);

    switch (ddi_func_ctrl.edp_input_select()) {
      case tgl_registers::TransDdiFuncControl::kPipeA:
        return At(tgl_registers::PIPE_A);
      case tgl_registers::TransDdiFuncControl::kPipeB:
        return At(tgl_registers::PIPE_B);
      case tgl_registers::TransDdiFuncControl::kPipeC:
        return At(tgl_registers::PIPE_C);
      default:
        // Not reachable
        ZX_DEBUG_ASSERT(false);
        return nullptr;
    }
  }

  for (Pipe* pipe : *this) {
    auto transcoder = static_cast<tgl_registers::Trans>(pipe->pipe());
    tgl_registers::TranscoderRegs regs(transcoder);
    if (regs.ClockSelect().ReadFrom(mmio_space).trans_clock_select() == ddi + 1u &&
        regs.DdiFuncControl().ReadFrom(mmio_space).ddi_select() == ddi) {
      return pipe;
    }
  }
  return nullptr;
}

// static
std::vector<std::unique_ptr<Pipe>> PipeManagerSkylake::GetPipes(fdf::MmioBuffer* mmio_space,
                                                                Power* power) {
  std::vector<std::unique_ptr<Pipe>> pipes;
  for (const auto pipe_enum : tgl_registers::Pipes<tgl_registers::Platform::kSkylake>()) {
    pipes.push_back(std::make_unique<PipeSkylake>(mmio_space, pipe_enum,
                                                  power->GetPipePowerWellRef(pipe_enum)));
  }
  return pipes;
}

}  // namespace i915_tgl
