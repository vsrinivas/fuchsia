// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <lib/gtest/test_loop_fixture.h>
#include <src/lib/fxl/strings/string_view.h>
#include <src/lib/fxl/strings/split_string.h>

#include "garnet/bin/memory_monitor/capture.h"
#include "garnet/bin/memory_monitor/printer.h"
#include "garnet/bin/memory_monitor/summary.h"
#include "garnet/bin/memory_monitor/test/test_utils.h"

namespace memory {
namespace test {

using PrinterUnitTest = gtest::TestLoopFixture;

void ConfirmLines(std::ostringstream& oss,
                  std::vector<std::string> expected_lines) {
  SCOPED_TRACE("");
  fxl::StringView lines_view(oss.str());
  auto lines = fxl::SplitStringCopy(
      lines_view, "\n", fxl::kKeepWhitespace, fxl::kSplitWantNonEmpty);
  ASSERT_EQ(expected_lines.size(), lines.size());
  for (size_t li = 0; li < expected_lines.size(); li++) {
    SCOPED_TRACE(li);
    std::string expected_line = expected_lines.at(li);
    fxl::StringView line = lines.at(li);
    EXPECT_STREQ(expected_line.c_str(), line.data());
  }
}

TEST_F(PrinterUnitTest, PrintCaptureKMEM) {
  Capture c;
  TestUtils::CreateCapture(c, {
    .time = 1234,
    .kmem = {
      .total_bytes = 300,
      .free_bytes = 100,
      .wired_bytes = 10,
      .total_heap_bytes = 20,
      .free_heap_bytes = 30,
      .vmo_bytes = 40,
      .mmu_overhead_bytes = 50,
      .ipc_bytes = 60,
      .other_bytes = 70
    },
    .vmos = {
      {.koid = 1, .name = "v1", .committed_bytes = 100},
    },
    .processes = {
      {.koid = 100, .name = "p1", .vmos = {1}},
    },
  });
  std::ostringstream oss;
  Printer p(oss);

  p.PrintCapture(c, KMEM, SORTED);
  ConfirmLines(oss, {
    "K,1234,300,100,10,20,30,40,50,60,70"
  });
}

TEST_F(PrinterUnitTest, PrintCapturePROCESS) {
  Capture c;
  TestUtils::CreateCapture(c, {
    .time = 1234,
    .kmem = {
      .total_bytes = 300,
      .free_bytes = 100,
      .wired_bytes = 10,
      .total_heap_bytes = 20,
      .free_heap_bytes = 30,
      .vmo_bytes = 40,
      .mmu_overhead_bytes = 50,
      .ipc_bytes = 60,
      .other_bytes = 70
    },
    .vmos = {
      {.koid = 1, .name = "v1", .committed_bytes = 100},
    },
    .processes = {
      {.koid = 100, .name = "p1", .vmos = {1}, .stats = {10, 20, 30, 40}},
    },
  });
  std::ostringstream oss;
  Printer p(oss);

  p.PrintCapture(c, PROCESS, SORTED);
  ConfirmLines(oss, {
    "K,1234,300,100,10,20,30,40,50,60,70",
    "P,100,p1,10,20,30,40,1"
  });
}

TEST_F(PrinterUnitTest, PrintCaptureVMO) {
  Capture c;
  TestUtils::CreateCapture(c, {
    .time = 1234,
    .kmem = {
      .total_bytes = 300,
      .free_bytes = 100,
      .wired_bytes = 10,
      .total_heap_bytes = 20,
      .free_heap_bytes = 30,
      .vmo_bytes = 40,
      .mmu_overhead_bytes = 50,
      .ipc_bytes = 60,
      .other_bytes = 70,
    },
    .vmos = {
      {
        .koid = 1,
        .name = "v1",
        .size_bytes = 100,
        .parent_koid = 200,
        .committed_bytes = 300,
      },
    },
    .processes = {
      {.koid = 100, .name = "p1", .vmos = {1}, .stats = {10, 20, 30, 40}},
    },
  });
  std::ostringstream oss;
  Printer p(oss);

  p.PrintCapture(c, VMO, SORTED);
  ConfirmLines(oss, {
    "K,1234,300,100,10,20,30,40,50,60,70",
    "P,100,p1,10,20,30,40,1",
    "V,1,v1,100,200,300",
  });
}

TEST_F(PrinterUnitTest, OutputSummarySingle) {
  Capture c;
  TestUtils::CreateCapture(c, {
    .time = 1234L * 1000000000L,
    .vmos = {
      {.koid = 1, .name = "v1", .committed_bytes = 100},
    },
    .processes = {
      {.koid = 100, .name = "p1", .vmos = {1}},
    },
  });
  Summary s(c);

  std::ostringstream oss;
  Printer p(oss);

  p.OutputSummary(s, SORTED, ZX_KOID_INVALID);
  ConfirmLines(oss, {
    "1234,100,p1,100,100,100",
  });

  oss.str("");
  p.OutputSummary(s, SORTED, 100);
  ConfirmLines(oss, {
    "1234,100,v1,100,100,100",
  });
}

TEST_F(PrinterUnitTest, OutputSummaryDouble) {
  Capture c;
  TestUtils::CreateCapture(c, {
    .time = 1234L * 1000000000L,
    .vmos = {
      {.koid = 1, .name = "v1", .committed_bytes = 100},
      {.koid = 2, .name = "v2", .committed_bytes = 200},
    },
    .processes = {
      {.koid = 100, .name = "p1", .vmos = {1}},
      {.koid = 200, .name = "p2", .vmos = {2}},
    },
  });
  Summary s(c);

  std::ostringstream oss;
  Printer p(oss);

  p.OutputSummary(s, SORTED, ZX_KOID_INVALID);
  ConfirmLines(oss, {
    "1234,200,p2,200,200,200",
    "1234,100,p1,100,100,100",
  });

  oss.str("");
  p.OutputSummary(s, SORTED, 100);
  ConfirmLines(oss, {
    "1234,100,v1,100,100,100",
  });

  oss.str("");
  p.OutputSummary(s, SORTED, 200);
  ConfirmLines(oss, {
    "1234,200,v2,200,200,200",
  });
}

TEST_F(PrinterUnitTest, OutputSummaryShared) {
  Capture c;
  TestUtils::CreateCapture(c, {
    .time = 1234L * 1000000000L,
    .vmos = {
      {.koid = 1, .name = "v1", .committed_bytes = 100},
      {.koid = 2, .name = "v1", .committed_bytes = 100},
      {.koid = 3, .name = "v1", .committed_bytes = 100},
      {.koid = 4, .name = "v2", .committed_bytes = 100},
      {.koid = 5, .name = "v3", .committed_bytes = 200},
    },
    .processes = {
      {.koid = 100, .name = "p1", .vmos = {1,2,4}},
      {.koid = 200, .name = "p2", .vmos = {2,3,5}},
    },
  });
  Summary s(c);

  std::ostringstream oss;
  Printer p(oss);

  p.OutputSummary(s, SORTED, ZX_KOID_INVALID);
  ConfirmLines(oss, {
    "1234,200,p2,300,350,400",
    "1234,100,p1,200,250,300",
  });

  oss.str("");
  p.OutputSummary(s, SORTED, 100);
  ConfirmLines(oss, {
    "1234,100,v1,100,150,200",
    "1234,100,v2,100,100,100",
  });

  oss.str("");
  p.OutputSummary(s, SORTED, 200);
  ConfirmLines(oss, {
    "1234,200,v3,200,200,200",
    "1234,200,v1,100,150,200",
  });
}

}  // namespace test
}  // namespace memory
