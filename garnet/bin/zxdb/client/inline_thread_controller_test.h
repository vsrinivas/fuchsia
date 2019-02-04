// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "garnet/bin/zxdb/client/mock_frame.h"
#include "garnet/bin/zxdb/client/thread_controller_test.h"
#include "garnet/bin/zxdb/common/address_ranges.h"
#include "garnet/bin/zxdb/symbols/location.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Function;
class MockFrame;

// Helper class used for testing thread controllers that need inline stacks.
//
//   [0] =   inline from frame 1: TopInline()
//   [1] = physical frame at kTopSP: Top()
//   [2] =   inline #2 from frame 4: MiddleInline2()
//   [3] =   inline #1 from frame 4: MiddleInline1()
//   [4] = physical frame at kMiddleSP: Middle()
//   [5] = physical frame at kBottomSP
//
// Note that MiddleInline1() and MiddleInline2() start at the same location
// (as if calling #2 was the first thing #1 did).
class InlineThreadControllerTest : public ThreadControllerTest {
 public:
  // Stack pointers for each physical frame.
  static const uint64_t kTopSP;
  static const uint64_t kMiddleSP;
  static const uint64_t kBottomSP;

  // Returns address range for each function.
  static const AddressRange kTopFunctionRange;
  static const AddressRange kTopInlineFunctionRange;
  static const AddressRange kMiddleFunctionRange;
  static const AddressRange kMiddleInline1FunctionRange;
  static const AddressRange kMiddleInline2FunctionRange;

  // Creates functions associated with each of the frames.
  static fxl::RefPtr<Function> GetTopFunction();
  static fxl::RefPtr<Function> GetTopInlineFunction();
  static fxl::RefPtr<Function> GetMiddleFunction();
  static fxl::RefPtr<Function> GetMiddleInline1Function();
  static fxl::RefPtr<Function> GetMiddleInline2Function();

  // Creates locations. The address is passed in and must be inside of the
  // range for the corresponding function.
  static Location GetTopLocation(uint64_t address);
  static Location GetTopInlineLocation(uint64_t address);
  static Location GetMiddleLocation(uint64_t address);
  static Location GetMiddleInline1Location(uint64_t address);
  static Location GetMiddleInline2Location(uint64_t address);

  // Constructor for frames.
  static std::unique_ptr<MockFrame> GetTopFrame(uint64_t address);
  static std::unique_ptr<MockFrame> GetTopInlineFrame(uint64_t address,
                                                      MockFrame* top);
  static std::unique_ptr<MockFrame> GetMiddleFrame(uint64_t address);
  static std::unique_ptr<MockFrame> GetMiddleInline1Frame(uint64_t address,
                                                          MockFrame* middle);
  static std::unique_ptr<MockFrame> GetMiddleInline2Frame(uint64_t address,
                                                          MockFrame* middle);
  static std::unique_ptr<MockFrame> GetBottomFrame(uint64_t address);

  // Constructs a fake stack. Even frame will have the address at the beginning
  // of its range.
  //
  // This function returns a vector of MockFrames so the caller can modify the
  // locations. It can then call MockFrameVectorToFrameVector() below to
  // convert to the frame vector other code expects.
  static std::vector<std::unique_ptr<MockFrame>> GetStack();

  // Downcasts a vector of owning MockFrame pointers to the corresponding Frame
  // pointers.
  static std::vector<std::unique_ptr<Frame>> MockFrameVectorToFrameVector(
      std::vector<std::unique_ptr<MockFrame>> mock_frames);

  // Adjusts the instruction pointer address of the given much frame by the
  // given offset. This is used to generate variants of the result of
  // GetStack().
  static void SetAddressForMockFrame(uint64_t address, MockFrame* mock_frame);
};

}  // namespace zxdb
