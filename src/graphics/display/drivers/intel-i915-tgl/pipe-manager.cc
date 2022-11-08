// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/pipe-manager.h"

#include <lib/mmio/mmio-buffer.h>
#include <zircon/assert.h>

#include <algorithm>
#include <optional>
#include <utility>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"
#include "src/graphics/display/drivers/intel-i915-tgl/intel-i915-tgl.h"
#include "src/graphics/display/drivers/intel-i915-tgl/pipe.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"
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
  Pipe* hw_state_pipe = GetPipeFromHwState(display.ddi_id(), mmio_space);
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
  bool edp_transcoder_in_use = false;
  for (Pipe* pipe : *this) {
    if (pipe->in_use()) {
      if (pipe->connected_transcoder_id() == TranscoderId::TRANSCODER_EDP) {
        edp_transcoder_in_use = true;

        const TranscoderId unused_transcoder_id = pipe->tied_transcoder_id();
        Pipe::ResetTranscoder(unused_transcoder_id, tgl_registers::Platform::kSkylake, mmio_space_);
        zxlogf(
            DEBUG,
            "Reset unused transcoder %d tied to pipe %d, which is connected to the EDP transcoder",
            unused_transcoder_id, pipe->pipe_id());
      }
    } else {
      Pipe::ResetTranscoder(pipe->tied_transcoder_id(), tgl_registers::Platform::kSkylake,
                            mmio_space_);
      zxlogf(DEBUG, "Reset unused transcoder %d tied to inactive pipe %d",
             pipe->tied_transcoder_id(), pipe->pipe_id());
    }
  }

  if (!edp_transcoder_in_use) {
    Pipe::ResetTranscoder(TranscoderId::TRANSCODER_EDP, tgl_registers::Platform::kSkylake,
                          mmio_space_);
    zxlogf(DEBUG, "Reset unused transcoder TRANSCODER_EDP (not used by any pipe)");
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

Pipe* PipeManagerSkylake::GetPipeFromHwState(DdiId ddi_id, fdf::MmioBuffer* mmio_space) {
  // On Kaby Lake and Skylake, DDI_A is attached to the EDP transcoder.
  if (ddi_id == DdiId::DDI_A) {
    tgl_registers::TranscoderRegs transcoder_regs(TranscoderId::TRANSCODER_EDP);
    auto transcoder_ddi_control = transcoder_regs.DdiControl().ReadFrom(mmio_space);

    const tgl_registers::Pipe pipe = transcoder_ddi_control.input_pipe();
    if (pipe == tgl_registers::Pipe::PIPE_INVALID) {
      // The transcoder DDI control register is configured incorrectly.
      return nullptr;
    }
    return At(pipe);
  }

  for (Pipe* pipe : *this) {
    const TranscoderId tied_transcoder = pipe->tied_transcoder_id();
    ZX_DEBUG_ASSERT_MSG(tied_transcoder != TranscoderId::TRANSCODER_EDP,
                        "The EDP transcoder is not tied to a pipe");

    tgl_registers::TranscoderRegs transcoder_regs(tied_transcoder);
    if (transcoder_regs.ClockSelect().ReadFrom(mmio_space).ddi_clock_kaby_lake() == ddi_id &&
        transcoder_regs.DdiControl().ReadFrom(mmio_space).ddi_kaby_lake() == ddi_id) {
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

PipeManagerTigerLake::PipeManagerTigerLake(Controller* controller)
    : PipeManager(GetPipes(controller->mmio_space(), controller->power())),
      mmio_space_(controller->mmio_space()) {
  ZX_DEBUG_ASSERT(controller);
}

Pipe* PipeManagerTigerLake::GetAvailablePipe() {
  for (Pipe* pipe : *this) {
    if (!pipe->in_use()) {
      return pipe;
    }
  }
  return nullptr;
}

Pipe* PipeManagerTigerLake::GetPipeFromHwState(DdiId ddi_id, fdf::MmioBuffer* mmio_space) {
  for (Pipe* pipe : *this) {
    auto transcoder_id = static_cast<TranscoderId>(pipe->pipe_id());
    tgl_registers::TranscoderRegs regs(transcoder_id);
    if (regs.ClockSelect().ReadFrom(mmio_space).ddi_clock_tiger_lake() == ddi_id &&
        regs.DdiControl().ReadFrom(mmio_space).ddi_tiger_lake() == ddi_id) {
      return pipe;
    }
  }
  return nullptr;
}

void PipeManagerTigerLake::ResetInactiveTranscoders() {
  for (Pipe* pipe : *this) {
    if (!pipe->in_use()) {
      Pipe::ResetTranscoder(pipe->connected_transcoder_id(), tgl_registers::Platform::kTigerLake,
                            mmio_space_);
      zxlogf(DEBUG, "Reset unused transcoder %d for pipe %d (pipe inactive)",
             pipe->connected_transcoder_id(), pipe->pipe_id());
    }
  }
}

// static
std::vector<std::unique_ptr<Pipe>> PipeManagerTigerLake::GetPipes(fdf::MmioBuffer* mmio_space,
                                                                  Power* power) {
  std::vector<std::unique_ptr<Pipe>> pipes;
  for (const auto pipe_enum : tgl_registers::Pipes<tgl_registers::Platform::kTigerLake>()) {
    pipes.push_back(std::make_unique<PipeTigerLake>(mmio_space, pipe_enum,
                                                    power->GetPipePowerWellRef(pipe_enum)));
  }
  return pipes;
}

}  // namespace i915_tgl
