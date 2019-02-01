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

// Tests fidl::Array<uint8_t> formatting.
TEST(FidlTypes, ArrayOfUint8) {
  std::ostringstream os;
  fidl::Array<uint8_t, 10> small_array;
  fidl::Array<uint8_t, 255> medium_array;
  fidl::Array<uint8_t, 265> large_array;
  for (uint8_t i = 0; i < 10; ++i) {
    small_array[i] = i;
    large_array[i] = i;
  }
  for (uint8_t i = 0; i < 255; ++i) {
    medium_array[i] = i;
    large_array[i + 10] = i;
  }

  os << Indent << "small:" << small_array << fostr::NewLine
     << "medium:" << medium_array << fostr::NewLine << "large:" << large_array;

  EXPECT_EQ(
      "small:"
      "\n    0000  00 01 02 03 04 05 06 07  08 09                    "
      "..........      "
      "\n    medium:"
      "\n    0000  00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f  "
      "................"
      "\n    0010  10 11 12 13 14 15 16 17  18 19 1a 1b 1c 1d 1e 1f  "
      "................"
      "\n    0020  20 21 22 23 24 25 26 27  28 29 2a 2b 2c 2d 2e 2f   "
      "!\"#$%&'()*+,-./"
      "\n    0030  30 31 32 33 34 35 36 37  38 39 3a 3b 3c 3d 3e 3f  "
      "0123456789:;<=>?"
      "\n    0040  40 41 42 43 44 45 46 47  48 49 4a 4b 4c 4d 4e 4f  "
      "@ABCDEFGHIJKLMNO"
      "\n    0050  50 51 52 53 54 55 56 57  58 59 5a 5b 5c 5d 5e 5f  "
      "PQRSTUVWXYZ[\\]^_"
      "\n    0060  60 61 62 63 64 65 66 67  68 69 6a 6b 6c 6d 6e 6f  "
      "`abcdefghijklmno"
      "\n    0070  70 71 72 73 74 75 76 77  78 79 7a 7b 7c 7d 7e 7f  "
      "pqrstuvwxyz{|}~."
      "\n    0080  80 81 82 83 84 85 86 87  88 89 8a 8b 8c 8d 8e 8f  "
      "................"
      "\n    0090  90 91 92 93 94 95 96 97  98 99 9a 9b 9c 9d 9e 9f  "
      "................"
      "\n    00a0  a0 a1 a2 a3 a4 a5 a6 a7  a8 a9 aa ab ac ad ae af  "
      "................"
      "\n    00b0  b0 b1 b2 b3 b4 b5 b6 b7  b8 b9 ba bb bc bd be bf  "
      "................"
      "\n    00c0  c0 c1 c2 c3 c4 c5 c6 c7  c8 c9 ca cb cc cd ce cf  "
      "................"
      "\n    00d0  d0 d1 d2 d3 d4 d5 d6 d7  d8 d9 da db dc dd de df  "
      "................"
      "\n    00e0  e0 e1 e2 e3 e4 e5 e6 e7  e8 e9 ea eb ec ed ee ef  "
      "................"
      "\n    00f0  f0 f1 f2 f3 f4 f5 f6 f7  f8 f9 fa fb fc fd fe     "
      "............... "
      "\n    large:"
      "\n    0000  00 01 02 03 04 05 06 07  08 09 00 01 02 03 04 05  "
      "................"
      "\n    0010  06 07 08 09 0a 0b 0c 0d  0e 0f 10 11 12 13 14 15  "
      "................"
      "\n    0020  16 17 18 19 1a 1b 1c 1d  1e 1f 20 21 22 23 24 25  "
      ".......... !\"#$%"
      "\n    0030  26 27 28 29 2a 2b 2c 2d  2e 2f 30 31 32 33 34 35  "
      "&'()*+,-./012345"
      "\n    (truncated, 265 bytes total)",

      os.str());
}

// Tests fidl::Array<int8_t> formatting.
TEST(FidlTypes, ArrayOfInt8) {
  std::ostringstream os;
  fidl::Array<int8_t, 10> small_array;
  fidl::Array<int8_t, 255> medium_array;
  fidl::Array<int8_t, 265> large_array;
  for (uint8_t i = 0; i < 10; ++i) {
    small_array[i] = static_cast<int8_t>(i);
    large_array[i] = static_cast<int8_t>(i);
  }
  for (uint8_t i = 0; i < 255; ++i) {
    medium_array[i] = static_cast<int8_t>(i);
    large_array[i + 10] = static_cast<int8_t>(i);
  }

  os << Indent << "small:" << small_array << fostr::NewLine
     << "medium:" << medium_array << fostr::NewLine << "large:" << large_array;

  EXPECT_EQ(
      "small:"
      "\n    0000  00 01 02 03 04 05 06 07  08 09                    "
      "..........      "
      "\n    medium:"
      "\n    0000  00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f  "
      "................"
      "\n    0010  10 11 12 13 14 15 16 17  18 19 1a 1b 1c 1d 1e 1f  "
      "................"
      "\n    0020  20 21 22 23 24 25 26 27  28 29 2a 2b 2c 2d 2e 2f   "
      "!\"#$%&'()*+,-./"
      "\n    0030  30 31 32 33 34 35 36 37  38 39 3a 3b 3c 3d 3e 3f  "
      "0123456789:;<=>?"
      "\n    0040  40 41 42 43 44 45 46 47  48 49 4a 4b 4c 4d 4e 4f  "
      "@ABCDEFGHIJKLMNO"
      "\n    0050  50 51 52 53 54 55 56 57  58 59 5a 5b 5c 5d 5e 5f  "
      "PQRSTUVWXYZ[\\]^_"
      "\n    0060  60 61 62 63 64 65 66 67  68 69 6a 6b 6c 6d 6e 6f  "
      "`abcdefghijklmno"
      "\n    0070  70 71 72 73 74 75 76 77  78 79 7a 7b 7c 7d 7e 7f  "
      "pqrstuvwxyz{|}~."
      "\n    0080  80 81 82 83 84 85 86 87  88 89 8a 8b 8c 8d 8e 8f  "
      "................"
      "\n    0090  90 91 92 93 94 95 96 97  98 99 9a 9b 9c 9d 9e 9f  "
      "................"
      "\n    00a0  a0 a1 a2 a3 a4 a5 a6 a7  a8 a9 aa ab ac ad ae af  "
      "................"
      "\n    00b0  b0 b1 b2 b3 b4 b5 b6 b7  b8 b9 ba bb bc bd be bf  "
      "................"
      "\n    00c0  c0 c1 c2 c3 c4 c5 c6 c7  c8 c9 ca cb cc cd ce cf  "
      "................"
      "\n    00d0  d0 d1 d2 d3 d4 d5 d6 d7  d8 d9 da db dc dd de df  "
      "................"
      "\n    00e0  e0 e1 e2 e3 e4 e5 e6 e7  e8 e9 ea eb ec ed ee ef  "
      "................"
      "\n    00f0  f0 f1 f2 f3 f4 f5 f6 f7  f8 f9 fa fb fc fd fe     "
      "............... "
      "\n    large:"
      "\n    0000  00 01 02 03 04 05 06 07  08 09 00 01 02 03 04 05  "
      "................"
      "\n    0010  06 07 08 09 0a 0b 0c 0d  0e 0f 10 11 12 13 14 15  "
      "................"
      "\n    0020  16 17 18 19 1a 1b 1c 1d  1e 1f 20 21 22 23 24 25  "
      ".......... !\"#$%"
      "\n    0030  26 27 28 29 2a 2b 2c 2d  2e 2f 30 31 32 33 34 35  "
      "&'()*+,-./012345"
      "\n    (truncated, 265 bytes total)",

      os.str());
}

// Tests fidl::VectorPtr formatting.
TEST(FidlTypes, VectorPtr) {
  std::ostringstream os;
  fidl::VectorPtr<std::string> null_vector;
  fidl::VectorPtr<std::string> empty_vector;
  fidl::VectorPtr<std::string> utensil_vector;
  empty_vector.resize(0);
  utensil_vector.push_back("knife");
  utensil_vector.push_back("spork");

  os << fostr::Indent << "null:" << null_vector << ", empty:" << empty_vector
     << ", utensil:" << utensil_vector;

  EXPECT_EQ(
      "null:<null>, empty:<empty>, utensil:"
      "\n    [0] knife"
      "\n    [1] spork",
      os.str());
}

// Tests fidl::VectorPtr<uint8_t> formatting.
TEST(FidlTypes, VectorPtrOfUint8) {
  std::ostringstream os;
  fidl::VectorPtr<uint8_t> null_vector;
  fidl::VectorPtr<uint8_t> empty_vector;
  fidl::VectorPtr<uint8_t> small_vector;
  fidl::VectorPtr<uint8_t> medium_vector;
  fidl::VectorPtr<uint8_t> large_vector;
  empty_vector.resize(0);
  for (uint8_t i = 0; i < 10; ++i) {
    small_vector.push_back(i);
    large_vector.push_back(i);
  }
  for (uint8_t i = 0; i < 255; ++i) {
    medium_vector.push_back(i);
    large_vector.push_back(i);
  }

  os << fostr::Indent << "null:" << null_vector << ", empty:" << empty_vector
     << fostr::NewLine << "small:" << small_vector << fostr::NewLine
     << "medium:" << medium_vector << fostr::NewLine
     << "large:" << large_vector;

  EXPECT_EQ(
      "null:<null>, empty:<empty>"
      "\n    small:"
      "\n    0000  00 01 02 03 04 05 06 07  08 09                    "
      "..........      "
      "\n    medium:"
      "\n    0000  00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f  "
      "................"
      "\n    0010  10 11 12 13 14 15 16 17  18 19 1a 1b 1c 1d 1e 1f  "
      "................"
      "\n    0020  20 21 22 23 24 25 26 27  28 29 2a 2b 2c 2d 2e 2f   "
      "!\"#$%&'()*+,-./"
      "\n    0030  30 31 32 33 34 35 36 37  38 39 3a 3b 3c 3d 3e 3f  "
      "0123456789:;<=>?"
      "\n    0040  40 41 42 43 44 45 46 47  48 49 4a 4b 4c 4d 4e 4f  "
      "@ABCDEFGHIJKLMNO"
      "\n    0050  50 51 52 53 54 55 56 57  58 59 5a 5b 5c 5d 5e 5f  "
      "PQRSTUVWXYZ[\\]^_"
      "\n    0060  60 61 62 63 64 65 66 67  68 69 6a 6b 6c 6d 6e 6f  "
      "`abcdefghijklmno"
      "\n    0070  70 71 72 73 74 75 76 77  78 79 7a 7b 7c 7d 7e 7f  "
      "pqrstuvwxyz{|}~."
      "\n    0080  80 81 82 83 84 85 86 87  88 89 8a 8b 8c 8d 8e 8f  "
      "................"
      "\n    0090  90 91 92 93 94 95 96 97  98 99 9a 9b 9c 9d 9e 9f  "
      "................"
      "\n    00a0  a0 a1 a2 a3 a4 a5 a6 a7  a8 a9 aa ab ac ad ae af  "
      "................"
      "\n    00b0  b0 b1 b2 b3 b4 b5 b6 b7  b8 b9 ba bb bc bd be bf  "
      "................"
      "\n    00c0  c0 c1 c2 c3 c4 c5 c6 c7  c8 c9 ca cb cc cd ce cf  "
      "................"
      "\n    00d0  d0 d1 d2 d3 d4 d5 d6 d7  d8 d9 da db dc dd de df  "
      "................"
      "\n    00e0  e0 e1 e2 e3 e4 e5 e6 e7  e8 e9 ea eb ec ed ee ef  "
      "................"
      "\n    00f0  f0 f1 f2 f3 f4 f5 f6 f7  f8 f9 fa fb fc fd fe     "
      "............... "
      "\n    large:"
      "\n    0000  00 01 02 03 04 05 06 07  08 09 00 01 02 03 04 05  "
      "................"
      "\n    0010  06 07 08 09 0a 0b 0c 0d  0e 0f 10 11 12 13 14 15  "
      "................"
      "\n    0020  16 17 18 19 1a 1b 1c 1d  1e 1f 20 21 22 23 24 25  "
      ".......... !\"#$%"
      "\n    0030  26 27 28 29 2a 2b 2c 2d  2e 2f 30 31 32 33 34 35  "
      "&'()*+,-./012345"
      "\n    (truncated, 265 bytes total)",
      os.str());
}

// Tests fidl::VectorPtr<int8_t> formatting.
TEST(FidlTypes, VectorPtrOfInt8) {
  std::ostringstream os;
  fidl::VectorPtr<int8_t> null_vector;
  fidl::VectorPtr<int8_t> empty_vector;
  fidl::VectorPtr<int8_t> small_vector;
  fidl::VectorPtr<int8_t> medium_vector;
  fidl::VectorPtr<int8_t> large_vector;
  empty_vector.resize(0);
  for (uint8_t i = 0; i < 10; ++i) {
    small_vector.push_back(static_cast<int8_t>(i));
    large_vector.push_back(static_cast<int8_t>(i));
  }
  for (uint8_t i = 0; i < 255; ++i) {
    medium_vector.push_back(static_cast<int8_t>(i));
    large_vector.push_back(static_cast<int8_t>(i));
  }

  os << fostr::Indent << "null:" << null_vector << ", empty:" << empty_vector
     << fostr::NewLine << "small:" << small_vector << fostr::NewLine
     << "medium:" << medium_vector << fostr::NewLine
     << "large:" << large_vector;

  EXPECT_EQ(
      "null:<null>, empty:<empty>"
      "\n    small:"
      "\n    0000  00 01 02 03 04 05 06 07  08 09                    "
      "..........      "
      "\n    medium:"
      "\n    0000  00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f  "
      "................"
      "\n    0010  10 11 12 13 14 15 16 17  18 19 1a 1b 1c 1d 1e 1f  "
      "................"
      "\n    0020  20 21 22 23 24 25 26 27  28 29 2a 2b 2c 2d 2e 2f   "
      "!\"#$%&'()*+,-./"
      "\n    0030  30 31 32 33 34 35 36 37  38 39 3a 3b 3c 3d 3e 3f  "
      "0123456789:;<=>?"
      "\n    0040  40 41 42 43 44 45 46 47  48 49 4a 4b 4c 4d 4e 4f  "
      "@ABCDEFGHIJKLMNO"
      "\n    0050  50 51 52 53 54 55 56 57  58 59 5a 5b 5c 5d 5e 5f  "
      "PQRSTUVWXYZ[\\]^_"
      "\n    0060  60 61 62 63 64 65 66 67  68 69 6a 6b 6c 6d 6e 6f  "
      "`abcdefghijklmno"
      "\n    0070  70 71 72 73 74 75 76 77  78 79 7a 7b 7c 7d 7e 7f  "
      "pqrstuvwxyz{|}~."
      "\n    0080  80 81 82 83 84 85 86 87  88 89 8a 8b 8c 8d 8e 8f  "
      "................"
      "\n    0090  90 91 92 93 94 95 96 97  98 99 9a 9b 9c 9d 9e 9f  "
      "................"
      "\n    00a0  a0 a1 a2 a3 a4 a5 a6 a7  a8 a9 aa ab ac ad ae af  "
      "................"
      "\n    00b0  b0 b1 b2 b3 b4 b5 b6 b7  b8 b9 ba bb bc bd be bf  "
      "................"
      "\n    00c0  c0 c1 c2 c3 c4 c5 c6 c7  c8 c9 ca cb cc cd ce cf  "
      "................"
      "\n    00d0  d0 d1 d2 d3 d4 d5 d6 d7  d8 d9 da db dc dd de df  "
      "................"
      "\n    00e0  e0 e1 e2 e3 e4 e5 e6 e7  e8 e9 ea eb ec ed ee ef  "
      "................"
      "\n    00f0  f0 f1 f2 f3 f4 f5 f6 f7  f8 f9 fa fb fc fd fe     "
      "............... "
      "\n    large:"
      "\n    0000  00 01 02 03 04 05 06 07  08 09 00 01 02 03 04 05  "
      "................"
      "\n    0010  06 07 08 09 0a 0b 0c 0d  0e 0f 10 11 12 13 14 15  "
      "................"
      "\n    0020  16 17 18 19 1a 1b 1c 1d  1e 1f 20 21 22 23 24 25  "
      ".......... !\"#$%"
      "\n    0030  26 27 28 29 2a 2b 2c 2d  2e 2f 30 31 32 33 34 35  "
      "&'()*+,-./012345"
      "\n    (truncated, 265 bytes total)",
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
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

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
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

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
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

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
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

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
