// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_PROTOCOL_BASE_H_
#define ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_PROTOCOL_BASE_H_

#include <type_traits>

#include <efi/types.h>
#include <gmock/gmock.h>

namespace efi {

// Base class to help wrap EFI protocols in gmock classes.
//
// This does a few nice things that we would otherwise have to repeat in each mock:
//   1. Provides a protocol() function to access the raw EFI protocol.
//   2. Provides a mechanism to bounce the C function into the C++ mock class
//
// Example usage:
// ==============
//   // Given this EFI protocol:
//   typedef struct efi_foo_protocol {
//     efi_status (*Foo)(struct efi_foo_protocol* self, char foo) EFIAPI;
//     efi_status (*Bar)(struct efi_foo_protocol* self, uint32_t* bar) EFIAPI;
//   } efi_foo_protocol;
//
//   // Defining the mock looks like this:
//   // 1. Use CRTP to make a MockProtocolBase sub-class.
//   class MockFooProtocol : public MockProtocolBase<MockFooProtocol, efi_foo_protocol> {
//    public:
//     // 2. In the constructor, use Bounce<> to set up each protocol function.
//     MockFooProtocol() : MockProtocolBase(
//         {.Foo = Bounce<&MockFooProtocol::Foo>,
//          .Bar = Bounce<&MockFooProtocol::Bar>}) {}
//
//     // 3. Wrap the protocol functions in MOCK_METHOD(), omitting the |self| param.
//     MOCK_METHOD(efi_status, Foo, (char foo));
//     MOCK_METHOD(efi_status, Bar, (uint32_t* bar));
//   };
// ==============
template <typename Derived, typename Protocol>
class MockProtocolBase {
 public:
  explicit MockProtocolBase(const Protocol& protocol)
      : wrapper_{protocol, static_cast<Derived*>(this)} {}

  virtual ~MockProtocolBase() = default;

  Protocol* protocol() { return &wrapper_; }

 protected:
  // Wraps the C protocol struct with a pointer to our C++ object so we can
  // locate the mock object from the C protocol pointer.
  struct Wrapper : public Protocol {
    constexpr explicit Wrapper(const Protocol& protocol, Derived* mock)
        : Protocol(protocol), mock_(mock) {}

    Derived* mock_ = nullptr;
  };

  // Bounces the EFI C function pointer into the mock function..
  template <auto Method, typename... Args>
  EFIAPI static efi_status Bounce(Protocol* self, Args... args) {
    return (static_cast<Wrapper*>(self)->mock_->*Method)(args...);
  }

  Wrapper wrapper_;
};

}  // namespace efi

#endif  // ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_PROTOCOL_BASE_H_
