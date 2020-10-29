// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UART_MOCK_H_
#define LIB_UART_MOCK_H_

// uart::mock::IoProvider supports testing uart::xyz::Driver hardware drivers.
// uart::mock::Driver supports testing uart::KernelDriver itself.
// It also serves to demonstrate the API required by uart::KernelDriver.

#include <lib/mock-function/mock-function.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <string_view>
#include <variant>

#include <hwreg/mock.h>

#include "uart.h"

namespace uart {
namespace mock {

// uart::KernelDriver IoProvider API
//
// This is used as the uart::KernelDriver IoProvider template for mock tests.
// When used with uart::mock::Driver, no actual I/O calls are ever made and
// this is just a placeholder.  When used with other uart::xyz::Driver
// hardware drivers, it provides the hwreg::Mock API for testing expected I/O
// calls from the driver.
template <typename Config>
class IoProvider {
 public:
  explicit IoProvider(const Config&, uint16_t pio_size) {}

  auto* io() { return io_.io(); }

  // Mock tests of hardware drivers use this to prime the mock with expected
  // callbacks from the driver.
  auto& mock() { return io_; }

 private:
  hwreg::Mock io_;
};

// uart::KernelDriver UartDriver API
//
// This pretends to be a hardware driver but is just a mock for tests.  If
// uart::mock::Sync is also used to instantiate uart::KernelDriver, then the
// expected synchronization calls are primed into the Driver mock so their
// ordering relative to the hardware driver calls can be tested.  The mock
// hardware Driver can also be used with other Sync API providers.
class Driver {
 public:
  struct config_type {};
  constexpr config_type config() const { return {}; }
  constexpr uint16_t pio_size() const { return 0; }

  // Fluent API for priming and checking the mock.

  Driver& ExpectInit() {
    mock_.ExpectCall({}, ExpectedInit{});
    return *this;
  }

  Driver& ExpectTxReady(bool ready) {
    mock_.ExpectCall(ready, ExpectedTxReady{});
    return *this;
  }

  // Note this takes the chars that the Write call will consume, not the chars
  // it expects to be called with.  The Write call might be passed more chars
  // and will consume (and verify) only this many.
  template <typename Char>
  Driver& ExpectWrite(const std::basic_string_view<Char> chars) {
    // A Write is modeled in mock_ as an ExpectedWrite yielding the count of
    // characters, and then a sequence of one ExpectedChar for each character.
    mock_.ExpectCall(chars.size(), ExpectedWrite{});
    for (auto c : chars) {
      static_assert(sizeof(Char) == sizeof(uint8_t));
      mock_.ExpectCall({}, ExpectedChar{static_cast<uint8_t>(c)});
    }
    return *this;
  }

  Driver& ExpectLock() {
    mock_.ExpectCall({}, ExpectedLock{false});
    return *this;
  }

  Driver& ExpectUnlock() {
    mock_.ExpectCall({}, ExpectedLock{true});
    return *this;
  }

  Driver& ExpectWait(bool block) {
    mock_.ExpectCall(block, ExpectedWait{});
    return *this;
  }

  Driver& ExpectEnableTxInterrupt() {
    mock_.ExpectCall({}, ExpectedTxEnable{});
    return *this;
  }

  void VerifyAndClear() { mock_.VerifyAndClear(); }

  ~Driver() { VerifyAndClear(); }

  // uart::KernelDriver UartDriver API
  //
  // Each method is a template parameterized by an an IoProvider type that
  // provides access to hwreg-compatible types accessing the hardware registers
  // via hwreg ReadFrom and WriteTo methods.  Real Driver types can be used
  // with hwreg::mock::IoProvider in tests independent of actual hardware
  // access.  The mock Driver to be used with hwreg::mock::IoProvider, but it
  // never makes any calls.

  void Init(IoProvider<config_type>& io) { mock_.Call(ExpectedInit{}); }

  // Return true if Write can make forward progress right now.
  bool TxReady(IoProvider<config_type>& io) {
    return std::get<bool>(mock_.Call(ExpectedTxReady{}));
  }

  // This is called only when TxReady() has just returned true.  Advance
  // the iterator at least one and as many as is convenient but not past
  // end, outputting each character before advancing.
  template <typename It1, typename It2>
  auto Write(IoProvider<config_type>& io, bool, It1 it, const It2& end) {
    for (auto n = std::get<size_t>(mock_.Call(ExpectedWrite{})); n > 0; --n) {
      ZX_ASSERT(it != end);
      mock_.Call(ExpectedChar{*it});
      ++it;
    }
    return it;
  }

  void EnableTxInterrupt(IoProvider<config_type>& io) { mock_.Call(ExpectedTxEnable{}); }

 private:
  template <typename Expected>
  struct ExpectedBase {
    bool operator==(Expected) const { return true; }
  };
  struct ExpectedLock {
    bool unlock = false;
    bool operator==(ExpectedLock other) const { return unlock == other.unlock; }
  };
  struct ExpectedWait : public ExpectedBase<ExpectedWait> {};
  struct ExpectedInit : public ExpectedBase<ExpectedInit> {};
  struct ExpectedTxEnable : public ExpectedBase<ExpectedTxEnable> {};
  struct ExpectedTxReady : public ExpectedBase<ExpectedTxReady> {};  // -> bool
  struct ExpectedWrite : public ExpectedBase<ExpectedWrite> {
  };  // -> size_t (count of ExpectedChar to follow)
  struct ExpectedChar {
    uint8_t c;
    bool operator==(ExpectedChar other) const { return c == other.c; }
  };
  using Expected = std::variant<ExpectedLock, ExpectedWait, ExpectedInit, ExpectedTxEnable,
                                ExpectedTxReady, ExpectedWrite, ExpectedChar>;
  using ExpectedResult = std::variant<bool, size_t>;
  mock_function::MockFunction<ExpectedResult, Expected> mock_;

  friend class Sync;
};

// uart::KernelDriver Sync API
//
// The expected calls are primed into the uart::mock::Driver in their
// appropriate ordering relative to calls into the Driver.
class TA_CAP("uart") Sync {
 public:
  struct InterruptState {};

  explicit Sync(Driver& driver) : mock_(driver.mock_) {}

  InterruptState lock() TA_ACQ() {
    mock_.Call(Driver::ExpectedLock{false});
    return {};
  }

  void unlock(InterruptState) TA_REL() { mock_.Call(Driver::ExpectedLock{true}); }

  template <typename T>
  InterruptState Wait(InterruptState, T&& enable_tx_interrupt)
      // TODO(mcgrathr): It doesn't grok that this is the same
      // capability that the enable_tx_interrupt lambda requires.
      TA_REQ(this) TA_NO_THREAD_SAFETY_ANALYSIS {
    if (std::get<bool>(mock_.Call(Driver::ExpectedWait{}))) {
      enable_tx_interrupt();
    }
    return {};
  }

 private:
  decltype(std::declval<Driver>().mock_)& mock_;
};

}  // namespace mock
}  // namespace uart

#endif  // LIB_UART_MOCK_H_
