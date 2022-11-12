// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_PIPE_MANAGER_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_PIPE_MANAGER_H_

#include <lib/mmio/mmio-buffer.h>

#include <iterator>
#include <list>
#include <type_traits>
#include <unordered_map>

#include "src/graphics/display/drivers/intel-i915-tgl/pipe.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-pipe.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-transcoder.h"

namespace i915_tgl {

class Controller;

template <bool IsConst, template <class, class...> class Container = std::vector>
class PipeIteratorBase {
 public:
  // Implement `std::iterator_traits`.
  using difference_type = int64_t;
  using value_type = std::conditional_t<IsConst, const Pipe*, Pipe*>;
  using pointer = void;
  using reference = std::add_lvalue_reference_t<value_type>;
  using iterator_category = std::forward_iterator_tag;

  using container_t = Container<std::unique_ptr<Pipe>>;
  using container_iterator_t = std::conditional_t<IsConst, typename container_t::const_iterator,
                                                  typename container_t::iterator>;

  explicit PipeIteratorBase(container_iterator_t it) : it_(it) {}

  explicit operator value_type() { return it_->get(); }
  value_type operator*() { return it_->get(); }

  bool operator!=(const PipeIteratorBase<IsConst>& other) const { return it_ != other.it_; }
  bool operator==(const PipeIteratorBase<IsConst>& other) const { return it_ == other.it_; }
  PipeIteratorBase<IsConst>& operator++() {
    ++it_;
    return *this;
  }
  PipeIteratorBase<IsConst> operator++(int) { return PipeIteratorBase<IsConst>(it_++); }

 private:
  container_iterator_t it_;
};

// `PipeManager` manages state of all `Pipe`s on the display engine.
//
// The set of `Pipe`s is defined at creation and is fixed and static over the
// lifetime of `PipeManager`. Callers can borrow a Pipe instance when they need
// it for display devices, and must return it before `PipeManager` is destroyed.
class PipeManager {
 public:
  virtual ~PipeManager() = default;

  PipeManager(PipeManager&&) = delete;
  PipeManager(const PipeManager&) = delete;

  // Request an unused Pipe for a new display, and attach the Pipe to |display|.
  //
  // Returned Pipes are guaranteed to outlive the display; On display removal,
  // The Pipe must be recycled by calling |ReturnPipe()|.
  //
  // Returns |nullptr| if there is no Pipe available.
  //
  // TODO(fxbug.dev/104985): This is error-prone because the caller has to call
  // `ReturnPipe()` to recycle used pipe manually. Instead we should add a
  // wrapper class (like `BorrowedPipeRef`) which could automatically return the
  // Pipe after use.
  Pipe* RequestPipe(DisplayDevice& display);

  // Request the Pipe that have been attached to |display| by other drivers
  // (i.e. BIOS / bootloader) by reading the pipe's hardware register state, and
  // then update the Pipe's state to note that it's attached to |display|.
  //
  // Returned Pipes are guaranteed to outlive the display; On display removal,
  // The Pipe must be recycled by calling |ReturnPipe()|.
  //
  // Returns |nullptr| if
  // - No Pipe has been ever attached to this |display|, or
  // - Error occurs when reading the hardware state.
  Pipe* RequestPipeFromHardwareState(DisplayDevice& display, fdf::MmioBuffer* mmio_space);

  // Reset pipe transcoders that are not actively in use, e.g. due to pipe being
  // inactive, or its corresponding pipe currently connects to another
  // transcoder.
  virtual void ResetInactiveTranscoders() = 0;

  // Return unused Pipe back to |PipeManager| when the display device is
  // removed; |pipe| must be a return value of previous |RequestPipe| or
  // |RequestPipeFromHardwareState| calls.
  void ReturnPipe(Pipe* pipe);

  // Returns whether there is any new Pipe allocated to displays, or unused
  // Pipe gets reset since last |PipeReallocated()| call.
  bool PipeReallocated();

  // Random accessor and forward iterator so that we can access the pipes
  // using <algorithms> methods and range-based for loop.
  //
  // TODO(fxbug.dev/104986): This (and the pipe iterator class) adds some
  // unnecessary complexity to the PipeManager; we can just replace it with a
  // method which returns `std::span<Pipe*>` instead.
  Pipe* operator[](PipeId idx) const;
  Pipe* At(PipeId idx) const;

  using PipeIterator = PipeIteratorBase<false>;
  using PipeConstIterator = PipeIteratorBase<true>;
  PipeIterator begin();
  PipeIterator end();
  PipeConstIterator begin() const;
  PipeConstIterator end() const;

 protected:
  explicit PipeManager(std::vector<std::unique_ptr<Pipe>> pipes);

  // Platform specific functions to get a new available pipe for an arbitrary
  // display device. Return |nullptr| if such a pipe is not available.
  virtual Pipe* GetAvailablePipe() = 0;

  // Platform specific functions to get a pipe that has been bound to this
  // DDI (usually by bootloader) for a display device.
  // Return |nullptr| if there is no such a pipe or if there is any other
  // internal error when loading the hardware state.
  virtual Pipe* GetPipeFromHwState(DdiId ddi_id, fdf::MmioBuffer* mmio_space) = 0;

 private:
  bool pipes_reallocated_ = false;
  std::vector<std::unique_ptr<Pipe>> pipes_;
};

// Instantiation of PipeManager for gen9 devices (Skylake, Kaby Lake, etc.)
class PipeManagerSkylake : public PipeManager {
 public:
  explicit PipeManagerSkylake(Controller* controller);
  ~PipeManagerSkylake() override = default;

  void ResetInactiveTranscoders() override;

 private:
  static constexpr PipeId kPipeEnums[] = {PipeId::PIPE_A, PipeId::PIPE_B, PipeId::PIPE_C};

  Pipe* GetAvailablePipe() override;
  Pipe* GetPipeFromHwState(DdiId ddi_id, fdf::MmioBuffer* mmio_space) override;

  static std::vector<std::unique_ptr<Pipe>> GetPipes(fdf::MmioBuffer* mmio_space, Power* power);

  fdf::MmioBuffer* mmio_space_;
};

// Instantiation of PipeManager for Tiger Lake
class PipeManagerTigerLake : public PipeManager {
 public:
  explicit PipeManagerTigerLake(Controller* controller);
  ~PipeManagerTigerLake() override = default;

  void ResetInactiveTranscoders() override;

 private:
  Pipe* GetAvailablePipe() override;
  Pipe* GetPipeFromHwState(DdiId ddi_id, fdf::MmioBuffer* mmio_space) override;

  static std::vector<std::unique_ptr<Pipe>> GetPipes(fdf::MmioBuffer* mmio_space, Power* power);

  fdf::MmioBuffer* mmio_space_;
};

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_PIPE_MANAGER_H_
