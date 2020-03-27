// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HWREG_MOCK_H_
#define HWREG_MOCK_H_

#include <lib/mock-function/mock-function.h>

#include <optional>
#include <type_traits>

namespace hwreg {

// The io() pointer can be passed to ReadFrom and WriteTo methods.  The
// Mock should first be primed with ExpectRead() and ExpectWrite() calls.

class Mock {
 private:
  // Forward declarations.
  struct DummyIo;
  class MockRegisterIo;

 public:
  using RegisterIo = std::variant<DummyIo, MockRegisterIo>;

  Mock() = default;
  Mock(Mock&& other) : mock_(std::move(other.mock_)) {}
  Mock& operator=(Mock&& other) {
    mock_ = std::move(other.mock_);
    return *this;
  }

  template <typename IntType>
  Mock& ExpectWrite(IntType value, uint32_t offset) {
    static_assert(internal::IsSupportedInt<IntType>::value, "unsupported register access width");
    mock_.ExpectCall(0, ExpectedWrite{sizeof(value), value}, offset);
    return *this;
  }

  template <typename IntType>
  Mock& ExpectRead(IntType value, uint32_t offset) {
    static_assert(internal::IsSupportedInt<IntType>::value, "unsupported register access width");
    mock_.ExpectCall(static_cast<uint64_t>(value), ExpectedRead{sizeof(value)}, offset);
    return *this;
  }

  Mock& ExpectNoIo() {
    mock_.ExpectNoCall();
    return *this;
  }

  void VerifyAndClear() { mock_.VerifyAndClear(); }

  RegisterIo* io() { return &io_; }

 private:
  struct ExpectedWrite {
    size_t size;
    uint64_t value;
    bool operator==(const ExpectedWrite& other) const {
      return other.size == size && other.value == value;
    }
  };

  struct ExpectedRead {
    size_t size;
    bool operator==(const ExpectedRead& other) const { return other.size == size; }
  };
  using ExpectedIo = std::variant<ExpectedWrite, ExpectedRead>;

  class MockRegisterIo {
   public:
    MockRegisterIo(const MockRegisterIo& other) : mock_(other.mock_) {}

    template <typename IntType>
    void Write(IntType value, uint32_t offset) const {
      static_assert(internal::IsSupportedInt<IntType>::value, "unsupported register access width");
      mock_.mock_.Call(ExpectedWrite{sizeof(value), value}, offset);
    }

    template <typename IntType>
    IntType Read(uint32_t offset) const {
      static_assert(internal::IsSupportedInt<IntType>::value, "unsupported register access width");
      return static_cast<IntType>(mock_.mock_.Call(ExpectedRead{sizeof(IntType)}, offset));
    }

   private:
    Mock& mock_;

    friend class Mock;
    explicit MockRegisterIo(Mock& mock) : mock_(mock) {}
  };

  // This is the default-constructed state of a hwreg::Mock::RegisterIo object.
  // Such members can't be used until they're copied from a Mock::io() result.
  struct DummyIo {
    template <typename IntType>
    void Write(IntType value, uint32_t offset) const {
      static_assert(internal::IsSupportedInt<IntType>::value, "unsupported register access width");
      ZX_PANIC("hwreg::Mock::RegisterIo used in default-constructed state");
    }

    template <typename IntType>
    IntType Read(uint32_t offset) const {
      static_assert(internal::IsSupportedInt<IntType>::value, "unsupported register access width");
      ZX_PANIC("hwreg::Mock::RegisterIo used in default-constructed state");
      return 0;
    }
  };

  RegisterIo io_{MockRegisterIo{*this}};

  mock_function::MockFunction<uint64_t, ExpectedIo, uint32_t> mock_;
};

}  // namespace hwreg

#endif  // HWREG_MOCK_H_
