// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fostr/fidl_types.h"

#include <sstream>

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include "lib/fsl/handles/object_info.h"

#include "gtest/gtest.h"

namespace fostr {
namespace {

// Matches string |value| from an istream.
std::istream& operator>>(std::istream& is, const std::string& value) {
  std::string str(value.size(), '\0');

  if (!is.read(&str[0], value.size()) || value != str) {
    return is;
  }

  // Required to set eofbit as appropriate.
  is.peek();

  return is;
}

// Tests fidl::Array formatting.
TEST(FidlTypes, Array) {
  std::ostringstream os;
  fidl::Array<std::string, 2> utensil_array;
  utensil_array[0] = "knife";
  utensil_array[1] = "spork";

  os << Indent << "utensil:" << utensil_array;

  EXPECT_EQ(
      "utensil:"
      "\n    [0] knife"
      "\n    [1] spork",
      os.str());
}

// Tests fidl::VectorPtr formatting.
TEST(FidlTypes, VectorPtr) {
  std::ostringstream os;
  fidl::VectorPtr<std::string> empty_vector;
  fidl::VectorPtr<std::string> utensil_vector;
  utensil_vector.push_back("knife");
  utensil_vector.push_back("spork");

  os << fostr::Indent << "empty:" << empty_vector
     << ", utensil:" << utensil_vector;

  EXPECT_EQ(
      "empty:<empty>, utensil:"
      "\n    [0] knife"
      "\n    [1] spork",
      os.str());
}

// Tests unbound Binding formatting.
TEST(FidlTypes, UnboundBinding) {
  std::ostringstream os;
  fuchsia::sys::ServiceProvider* impl = nullptr;
  fidl::Binding<fuchsia::sys::ServiceProvider> binding(impl);

  os << binding;
  EXPECT_EQ("<not bound>", os.str());
}

// Tests Binding formatting.
TEST(FidlTypes, Binding) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  std::ostringstream os;
  fuchsia::sys::ServiceProvider* impl = nullptr;
  fidl::Binding<fuchsia::sys::ServiceProvider> binding(impl);
  auto interface_handle = binding.NewBinding();

  os << binding;

  std::istringstream is(os.str());
  zx_koid_t koid;
  zx_koid_t related_koid;
  is >> "koid 0x" >> std::hex >> koid >> " <-> 0x" >> related_koid;

  EXPECT_TRUE(is && is.eof());
  EXPECT_EQ(fsl::GetKoid(binding.channel().get()), koid);
  EXPECT_EQ(fsl::GetKoid(interface_handle.channel().get()), related_koid);
}

// Tests invalid InterfaceHandle formatting.
TEST(FidlTypes, UnboundInterfaceHandle) {
  std::ostringstream os;
  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> interface_handle;

  os << interface_handle;
  EXPECT_EQ("<not valid>", os.str());
}

// Tests InterfaceHandle formatting.
TEST(FidlTypes, InterfaceHandle) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  std::ostringstream os;
  fuchsia::sys::ServiceProvider* impl = nullptr;
  fidl::Binding<fuchsia::sys::ServiceProvider> binding(impl);
  auto interface_handle = binding.NewBinding();

  os << interface_handle;

  std::istringstream is(os.str());
  zx_koid_t koid;
  zx_koid_t related_koid;
  is >> "koid 0x" >> std::hex >> koid >> " <-> 0x" >> related_koid;

  EXPECT_TRUE(is && is.eof());
  EXPECT_EQ(fsl::GetKoid(interface_handle.channel().get()), koid);
  EXPECT_EQ(fsl::GetKoid(binding.channel().get()), related_koid);
}

// Tests unbound InterfacePtr formatting.
TEST(FidlTypes, UnboundInterfacePtr) {
  std::ostringstream os;
  fidl::InterfacePtr<fuchsia::sys::ServiceProvider> interface_ptr;

  os << interface_ptr;
  EXPECT_EQ("<not bound>", os.str());
}

// Tests InterfacePtr formatting.
TEST(FidlTypes, InterfacePtr) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  std::ostringstream os;
  fidl::InterfacePtr<fuchsia::sys::ServiceProvider> interface_ptr;
  auto interface_request = interface_ptr.NewRequest();

  os << interface_ptr;

  std::istringstream is(os.str());
  zx_koid_t koid;
  zx_koid_t related_koid;
  is >> "koid 0x" >> std::hex >> koid >> " <-> 0x" >> related_koid;

  EXPECT_TRUE(is && is.eof());
  EXPECT_EQ(fsl::GetKoid(interface_ptr.channel().get()), koid);
  EXPECT_EQ(fsl::GetKoid(interface_request.channel().get()), related_koid);
}

// Tests invalid InterfaceRequest formatting.
TEST(FidlTypes, InvalidInterfaceRequest) {
  std::ostringstream os;
  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> interface_request;

  os << interface_request;
  EXPECT_EQ("<not valid>", os.str());
}

// Tests InterfaceRequest formatting.
TEST(FidlTypes, InterfaceRequest) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  std::ostringstream os;
  fidl::InterfacePtr<fuchsia::sys::ServiceProvider> interface_ptr;
  auto interface_request = interface_ptr.NewRequest();

  os << interface_request;

  std::istringstream is(os.str());
  zx_koid_t koid;
  zx_koid_t related_koid;
  is >> "koid 0x" >> std::hex >> koid >> " <-> 0x" >> related_koid;

  EXPECT_TRUE(is && is.eof());
  EXPECT_EQ(fsl::GetKoid(interface_request.channel().get()), koid);
  EXPECT_EQ(fsl::GetKoid(interface_ptr.channel().get()), related_koid);
}

}  // namespace
}  // namespace fostr
