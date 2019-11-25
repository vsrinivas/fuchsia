// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_INLINE_THREAD_CONTROLLER_TEST_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_INLINE_THREAD_CONTROLLER_TEST_H_

#include <vector>

#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/thread_controller_test.h"
#include "src/developer/debug/zxdb/common/address_ranges.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Function;
class MockFrame;

// Helper class used for testing thread controllers that need inline stacks.
//
// Note on code locations: The source location for inline calls and physical calls is different. The
// current instruction for a non-topmost physical frame is always the return address of the function
// call (typically the next line) because the debuggers knows the return address but don't
// necessarily know the exact call location. For inline calls, however, we show the inline call
// location because we do have that information, but don't know exactly where the inline call will
// "return" to since there's no clear return address.
//
// The code looks like this, with line numbers and the code locations (see note above):
//
//   10  inline void TopInline() {
//   11    ...                          <- kTopInlineFileLine
//   12  }
//   13  void Top() {
//   14    ...
//   15    TopInlineFunction();         <- kTopFileLine
//   16    ...
//   17  }
//   18
//   19  inline void MiddleInline2() {
//   20    ...
//   21    Top();  // Non-inline call.
//   22    ...                          <- kMiddleInline2FileLine
//   23  }
//   24  inline void MiddleInline1() {
//   25    MiddleInline2();             <- kMiddleInline1FileLine
//   26    ...
//   27  }
//   28  void Middle() {
//   29    ...
//   30    MiddleInline1();             <- kMiddleFileLine
//   31    ...
//   32  }
//   33
//   34  void Bottom() {
//   35    ...
//   36    Middle();
//   37    ...
//   38  }
//
// The stack looks like this:
//
//   [0] =   inline from frame 1: TopInline()
//   [1] = physical frame at kTopSP: Top()
//   [2] =   inline #2 from frame 4: MiddleInline2()
//   [3] =   inline #1 from frame 4: MiddleInline1()
//   [4] = physical frame at kMiddleSP: Middle()
//   [5] = physical frame at kBottomSP
//
// Binary code layout
//
//   +--------------------------+
//   | TopFunction              |
//   |                          |
//   |  +--------------------+  |
//   |  | TopInlineFunction  |  |
//   |  +--------------------+  |
//   +--------------------------+
//
//   +----------------------------------------------------------+
//   | MiddleFunction                                           |
//   |                                                          |
//   |  +------------------------+------------------------+--+  |
//   |  | MiddleFunctionInline1  | MiddleFunctionInline2  |  |  |
//   |  |                        +------------------------+  |  |
//   |  |                                                    |  |
//   |  +----------------------------------------------------+  |
//   |                                                          |
//   +----------------------------------------------------------+
//
// Note that MiddleInline1() and MiddleInline2() start at the same location
// (as if calling #2 was the first thing #1 did).
class InlineThreadControllerTest : public ThreadControllerTest {
 public:
  // Stack pointers for each physical frame.
  static const uint64_t kTopSP;
  static const uint64_t kMiddleSP;
  static const uint64_t kBottomSP;

  // Address range for each function.
  static const AddressRange kTopFunctionRange;
  static const AddressRange kTopInlineFunctionRange;
  static const AddressRange kMiddleFunctionRange;
  static const AddressRange kMiddleInline1FunctionRange;
  static const AddressRange kMiddleInline2FunctionRange;

  // Line numbers.
  static const FileLine kTopInlineFileLine;      // IP @ top of stack
  static const FileLine kTopFileLine;            // Call loc of top inline.
  static const FileLine kMiddleInline2FileLine;  // Return of Top().
  static const FileLine kMiddleInline1FileLine;  // Call loc of inline2.
  static const FileLine kMiddleFileLine;         // Call loc of inline1.

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
  static std::unique_ptr<MockFrame> GetTopInlineFrame(uint64_t address, MockFrame* top);
  static std::unique_ptr<MockFrame> GetMiddleFrame(uint64_t address);
  static std::unique_ptr<MockFrame> GetMiddleInline1Frame(uint64_t address, MockFrame* middle);
  static std::unique_ptr<MockFrame> GetMiddleInline2Frame(uint64_t address, MockFrame* middle);
  static std::unique_ptr<MockFrame> GetBottomFrame(uint64_t address);

  // Constructs a fake stack. Even frame will have the address at the beginning of its range.
  //
  // This function returns a vector of MockFrames so the caller can modify the locations. It can
  // then call MockFrameVectorToFrameVector() below to convert to the frame vector other code
  // expects.
  static std::vector<std::unique_ptr<MockFrame>> GetStack();

  // Downcasts a vector of owning MockFrame pointers to the corresponding Frame pointers.
  static std::vector<std::unique_ptr<Frame>> MockFrameVectorToFrameVector(
      std::vector<std::unique_ptr<MockFrame>> mock_frames);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_INLINE_THREAD_CONTROLLER_TEST_H_
