// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/inline_thread_controller_test.h"

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"

namespace zxdb {

namespace {

fxl::RefPtr<Function> MakeFunction(const char* name, bool is_inline, AddressRanges ranges) {
  DwarfTag tag = is_inline ? DwarfTag::kInlinedSubroutine : DwarfTag::kSubprogram;
  auto func = fxl::MakeRefCounted<Function>(tag);
  func->set_assigned_name(name);
  func->set_code_ranges(std::move(ranges));
  return func;
}

}  // namespace

const uint64_t InlineThreadControllerTest::kTopSP = 0x2010;
const uint64_t InlineThreadControllerTest::kMiddleSP = 0x2020;
const uint64_t InlineThreadControllerTest::kBottomSP = 0x2040;

// These address ranges must all be inside the symbolized module address so tests can mock symbols
// and line lookups inside of them.
const AddressRange InlineThreadControllerTest::kTopFunctionRange(kSymbolizedModuleAddress + 0x30000,
                                                                 kSymbolizedModuleAddress +
                                                                     0x40000);
// Must be inside the top function.
const AddressRange InlineThreadControllerTest::kTopInlineFunctionRange(
    kSymbolizedModuleAddress + 0x30100, kSymbolizedModuleAddress + 0x30200);
const AddressRange InlineThreadControllerTest::kMiddleFunctionRange(
    kSymbolizedModuleAddress + 0x10000, kSymbolizedModuleAddress + 0x20000);
// Must be inside the middle function
const AddressRange InlineThreadControllerTest::kMiddleInline1FunctionRange(
    kSymbolizedModuleAddress + 0x10100, kSymbolizedModuleAddress + 0x10200);
// Must be inside the middle inline 1 function with same start address.
const AddressRange InlineThreadControllerTest::kMiddleInline2FunctionRange(
    kSymbolizedModuleAddress + 0x10100, kSymbolizedModuleAddress + 0x10110);

// Note that the Stack object currently treats the location of caller of an inline frame to be the
// inline call site, while for physical frames this will be the following line. The reason for the
// difference is that inline functions don't necessarily have a clear return address, and the actual
// call is the easiest thing to compute.
const FileLine InlineThreadControllerTest::kTopInlineFileLine("file.cc", 11);
const FileLine InlineThreadControllerTest::kTopFileLine("file.cc", 15);
const FileLine InlineThreadControllerTest::kMiddleInline2FileLine("file.cc", 22);
const FileLine InlineThreadControllerTest::kMiddleInline1FileLine("file.cc", 25);
const FileLine InlineThreadControllerTest::kMiddleFileLine("file.cc", 30);

// static
fxl::RefPtr<Function> InlineThreadControllerTest::GetTopFunction() {
  return MakeFunction("Top", false, AddressRanges(kTopFunctionRange));
}

// static
fxl::RefPtr<Function> InlineThreadControllerTest::GetTopInlineFunction() {
  auto func = MakeFunction("TopInline", true, AddressRanges(kTopInlineFunctionRange));
  func->set_call_line(kTopFileLine);
  return func;
}

// static
fxl::RefPtr<Function> InlineThreadControllerTest::GetMiddleFunction() {
  return MakeFunction("Middle", false, AddressRanges(kMiddleFunctionRange));
}

// static
fxl::RefPtr<Function> InlineThreadControllerTest::GetMiddleInline1Function() {
  auto func = MakeFunction("MiddleInline1", true, AddressRanges(kMiddleInline1FunctionRange));
  func->set_call_line(kMiddleFileLine);
  return func;
}

// static
fxl::RefPtr<Function> InlineThreadControllerTest::GetMiddleInline2Function() {
  auto func = MakeFunction("MiddleInline2", true, AddressRanges(kMiddleInline2FunctionRange));
  func->set_call_line(kMiddleInline1FileLine);
  return func;
}

// static
Location InlineThreadControllerTest::GetTopLocation(uint64_t address) {
  return Location(address, kTopFileLine, 0, SymbolContext::ForRelativeAddresses(),
                  GetTopFunction());
}

// static
Location InlineThreadControllerTest::GetTopInlineLocation(uint64_t address) {
  return Location(address, kTopInlineFileLine, 0, SymbolContext::ForRelativeAddresses(),
                  GetTopInlineFunction());
}

// static
Location InlineThreadControllerTest::GetMiddleLocation(uint64_t address) {
  return Location(address, kMiddleFileLine, 0, SymbolContext::ForRelativeAddresses(),
                  GetMiddleFunction());
}

// static
Location InlineThreadControllerTest::GetMiddleInline1Location(uint64_t address) {
  return Location(address, kMiddleInline1FileLine, 0, SymbolContext::ForRelativeAddresses(),
                  GetMiddleInline1Function());
}

// static
Location InlineThreadControllerTest::GetMiddleInline2Location(uint64_t address) {
  return Location(address, kMiddleInline2FileLine, 0, SymbolContext::ForRelativeAddresses(),
                  GetMiddleInline2Function());
}

// static
std::unique_ptr<MockFrame> InlineThreadControllerTest::GetTopFrame(uint64_t address) {
  return std::make_unique<MockFrame>(nullptr, nullptr, GetTopLocation(address), kTopSP, kMiddleSP);
}

// static
std::unique_ptr<MockFrame> InlineThreadControllerTest::GetTopInlineFrame(uint64_t address,
                                                                         MockFrame* top) {
  // The location is ambiguous if the address is at the beginning of the range.
  return std::make_unique<MockFrame>(nullptr, nullptr, GetTopInlineLocation(address), kTopSP,
                                     kMiddleSP, std::vector<debug_ipc::Register>(), kTopSP, top,
                                     address == kTopInlineFunctionRange.begin());
}

// static
std::unique_ptr<MockFrame> InlineThreadControllerTest::GetMiddleFrame(uint64_t address) {
  return std::make_unique<MockFrame>(nullptr, nullptr, GetMiddleLocation(address), kMiddleSP,
                                     kBottomSP, std::vector<debug_ipc::Register>(), kMiddleSP);
}

// static
std::unique_ptr<MockFrame> InlineThreadControllerTest::GetMiddleInline1Frame(uint64_t address,
                                                                             MockFrame* middle) {
  return std::make_unique<MockFrame>(nullptr, nullptr, GetMiddleInline1Location(address), kMiddleSP,
                                     kBottomSP, std::vector<debug_ipc::Register>(), kMiddleSP,
                                     middle, address == kMiddleInline1FunctionRange.begin());
}

// static
std::unique_ptr<MockFrame> InlineThreadControllerTest::GetMiddleInline2Frame(uint64_t address,
                                                                             MockFrame* middle) {
  return std::make_unique<MockFrame>(nullptr, nullptr, GetMiddleInline2Location(address), kMiddleSP,
                                     kBottomSP, std::vector<debug_ipc::Register>(), kMiddleSP,
                                     middle, address == kMiddleInline2FunctionRange.begin());
}

// static
std::unique_ptr<MockFrame> InlineThreadControllerTest::GetBottomFrame(uint64_t address) {
  return std::make_unique<MockFrame>(nullptr, nullptr,
                                     Location(Location::State::kSymbolized, address), kBottomSP);
}

// static
std::vector<std::unique_ptr<MockFrame>> InlineThreadControllerTest::GetStack() {
  AddressRange top_inline_range = kTopInlineFunctionRange;
  auto top_frame = GetTopFrame(top_inline_range.begin());

  AddressRange top_middle_inline2_range = kMiddleInline2FunctionRange;
  auto middle_frame = GetMiddleFrame(top_middle_inline2_range.begin());

  std::vector<std::unique_ptr<MockFrame>> frames;
  frames.push_back(GetTopInlineFrame(top_inline_range.begin(), top_frame.get()));
  frames.push_back(std::move(top_frame));
  // These inlined functions in the middle of the stack must not be ambiguous because the stack will
  // never generate ambiguous inlined functions for anything but the top frame. To do this, the
  // address bust be after the beginning of the code range.
  frames.push_back(GetMiddleInline2Frame(top_middle_inline2_range.begin() + 1, middle_frame.get()));
  frames.push_back(GetMiddleInline1Frame(top_middle_inline2_range.begin() + 1, middle_frame.get()));
  frames.push_back(std::move(middle_frame));
  frames.push_back(GetBottomFrame(0x100000000));

  return frames;
}

// static
std::vector<std::unique_ptr<Frame>> InlineThreadControllerTest::MockFrameVectorToFrameVector(
    std::vector<std::unique_ptr<MockFrame>> mock_frames) {
  std::vector<std::unique_ptr<Frame>> frames;
  for (auto& mf : mock_frames)
    frames.push_back(std::move(mf));
  return frames;
}

}  // namespace zxdb
