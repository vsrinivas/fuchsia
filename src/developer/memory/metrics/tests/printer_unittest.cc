// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/metrics/printer.h"

#include <gtest/gtest.h>

#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/summary.h"
#include "src/developer/memory/metrics/tests/test_utils.h"
#include "src/lib/fxl/strings/split_string.h"
#include "third_party/rapidjson/include/rapidjson/document.h"

namespace memory {
namespace test {

using PrinterUnitTest = testing::Test;

void ConfirmLines(std::ostringstream& oss, std::vector<std::string> expected_lines) {
  SCOPED_TRACE("");
  auto lines = fxl::SplitStringCopy(oss.str(), "\n", fxl::kKeepWhitespace, fxl::kSplitWantNonEmpty);
  ASSERT_EQ(expected_lines.size(), lines.size());
  for (size_t li = 0; li < expected_lines.size(); li++) {
    SCOPED_TRACE(li);
    std::string expected_line = expected_lines.at(li);
    std::string_view line = lines.at(li);
    EXPECT_STREQ(expected_line.c_str(), line.data());
  }
}

TEST_F(PrinterUnitTest, PrintCapture) {
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .time = 1234,
                                   .kmem =
                                       {
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
                                   .kmem_extended =
                                       {
                                           .total_bytes = 300,
                                           .free_bytes = 100,
                                           .wired_bytes = 10,
                                           .total_heap_bytes = 20,
                                           .free_heap_bytes = 30,
                                           .vmo_bytes = 40,
                                           .vmo_pager_total_bytes = 15,
                                           .vmo_pager_newest_bytes = 4,
                                           .vmo_pager_oldest_bytes = 8,
                                           .vmo_discardable_locked_bytes = 3,
                                           .vmo_discardable_unlocked_bytes = 7,
                                           .mmu_overhead_bytes = 50,
                                           .ipc_bytes = 60,
                                           .other_bytes = 70,
                                       },
                                   .vmos =
                                       {
                                           {
                                               .koid = 1,
                                               .name = "v1",
                                               .size_bytes = 300,
                                               .parent_koid = 100,
                                               .committed_bytes = 200,
                                           },
                                       },
                                   .processes =
                                       {
                                           {.koid = 100, .name = "p1", .vmos = {1}},
                                       },
                               });
  std::ostringstream oss;
  Printer p(oss);

  p.PrintCapture(c);

  rapidjson::Document doc;
  doc.Parse(oss.str().c_str());

  ASSERT_TRUE(doc.IsObject());

  EXPECT_EQ(1234, doc["Time"].GetInt64());

  auto kernel = doc["Kernel"].GetObject();
  EXPECT_EQ(300U, kernel["total"].GetUint64());
  EXPECT_EQ(100U, kernel["free"].GetUint64());
  EXPECT_EQ(10U, kernel["wired"].GetUint64());
  EXPECT_EQ(20U, kernel["total_heap"].GetUint64());
  EXPECT_EQ(30U, kernel["free_heap"].GetUint64());
  EXPECT_EQ(40U, kernel["vmo"].GetUint64());
  EXPECT_EQ(50U, kernel["mmu"].GetUint64());
  EXPECT_EQ(60U, kernel["ipc"].GetUint64());
  EXPECT_EQ(70U, kernel["other"].GetUint64());
  EXPECT_EQ(15U, kernel["vmo_pager_total"].GetUint64());
  EXPECT_EQ(4U, kernel["vmo_pager_newest"].GetUint64());
  EXPECT_EQ(8U, kernel["vmo_pager_oldest"].GetUint64());
  EXPECT_EQ(3U, kernel["vmo_discardable_locked"].GetUint64());
  EXPECT_EQ(7U, kernel["vmo_discardable_unlocked"].GetUint64());

  auto processes = doc["Processes"].GetArray();
  ASSERT_EQ(2U, processes.Size());
  auto process_header = processes[0].GetArray();
  EXPECT_STREQ("koid", process_header[0].GetString());
  EXPECT_STREQ("name", process_header[1].GetString());
  EXPECT_STREQ("vmos", process_header[2].GetString());
  auto process = processes[1].GetArray();
  EXPECT_EQ(100U, process[0].GetUint64());
  EXPECT_STREQ("p1", process[1].GetString());
  auto process_vmos = process[2].GetArray();
  ASSERT_EQ(1U, process_vmos.Size());
  EXPECT_EQ(1, process_vmos[0]);

  auto vmo_names = doc["VmoNames"].GetArray();
  ASSERT_EQ(1U, vmo_names.Size());
  EXPECT_STREQ("v1", vmo_names[0].GetString());

  auto vmos = doc["Vmos"].GetArray();
  ASSERT_EQ(2U, vmos.Size());
  auto vmo_header = vmos[0].GetArray();
  EXPECT_STREQ("koid", vmo_header[0].GetString());
  EXPECT_STREQ("name", vmo_header[1].GetString());
  EXPECT_STREQ("parent_koid", vmo_header[2].GetString());
  EXPECT_STREQ("committed_bytes", vmo_header[3].GetString());
  EXPECT_STREQ("allocated_bytes", vmo_header[4].GetString());
  auto vmo = vmos[1].GetArray();
  EXPECT_EQ(1U, vmo[0].GetUint64());
  EXPECT_EQ(0U, vmo[1].GetUint64());
  EXPECT_EQ(100U, vmo[2].GetUint64());
  EXPECT_EQ(200U, vmo[3].GetUint64());
  EXPECT_EQ(300U, vmo[4].GetUint64());
}

TEST_F(PrinterUnitTest, PrintCaptureAndBucketConfig) {
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .time = 1234,
                                   .kmem =
                                       {
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
                                   .kmem_extended =
                                       {
                                           .total_bytes = 300,
                                           .free_bytes = 100,
                                           .wired_bytes = 10,
                                           .total_heap_bytes = 20,
                                           .free_heap_bytes = 30,
                                           .vmo_bytes = 40,
                                           .vmo_pager_total_bytes = 15,
                                           .vmo_pager_newest_bytes = 4,
                                           .vmo_pager_oldest_bytes = 8,
                                           .vmo_discardable_locked_bytes = 3,
                                           .vmo_discardable_unlocked_bytes = 7,
                                           .mmu_overhead_bytes = 50,
                                           .ipc_bytes = 60,
                                           .other_bytes = 70,
                                       },
                                   .vmos =
                                       {
                                           {
                                               .koid = 1,
                                               .name = "v1",
                                               .size_bytes = 300,
                                               .parent_koid = 100,
                                               .committed_bytes = 200,
                                           },
                                       },
                                   .processes =
                                       {
                                           {.koid = 100, .name = "p1", .vmos = {1}},
                                       },
                               });
  std::ostringstream oss;
  Printer p(oss);

  const std::string bucket_config = R"(
    [
        {
            "event_code" : 29,
            "name" : "BlobfsInactive",
            "process" : "blobfs\\.cm",
            "vmo": "inactive-blob-.*"
        }
    ]
  )";
  p.PrintCaptureAndBucketConfig(c, bucket_config);

  rapidjson::Document doc;
  doc.Parse(oss.str().c_str());
  ASSERT_TRUE(doc.IsObject());

  rapidjson::Value const& capture = doc["Capture"];
  rapidjson::Value const& buckets = doc["Buckets"];

  // Test the content of `capture`.
  ASSERT_TRUE(capture.IsObject());

  EXPECT_EQ(1234, capture["Time"].GetInt64());

  auto kernel = capture["Kernel"].GetObject();
  EXPECT_EQ(300U, kernel["total"].GetUint64());
  EXPECT_EQ(100U, kernel["free"].GetUint64());
  EXPECT_EQ(10U, kernel["wired"].GetUint64());
  EXPECT_EQ(20U, kernel["total_heap"].GetUint64());
  EXPECT_EQ(30U, kernel["free_heap"].GetUint64());
  EXPECT_EQ(40U, kernel["vmo"].GetUint64());
  EXPECT_EQ(50U, kernel["mmu"].GetUint64());
  EXPECT_EQ(60U, kernel["ipc"].GetUint64());
  EXPECT_EQ(70U, kernel["other"].GetUint64());
  EXPECT_EQ(15U, kernel["vmo_pager_total"].GetUint64());
  EXPECT_EQ(4U, kernel["vmo_pager_newest"].GetUint64());
  EXPECT_EQ(8U, kernel["vmo_pager_oldest"].GetUint64());
  EXPECT_EQ(3U, kernel["vmo_discardable_locked"].GetUint64());
  EXPECT_EQ(7U, kernel["vmo_discardable_unlocked"].GetUint64());

  auto processes = capture["Processes"].GetArray();
  ASSERT_EQ(2U, processes.Size());
  auto process_header = processes[0].GetArray();
  EXPECT_STREQ("koid", process_header[0].GetString());
  EXPECT_STREQ("name", process_header[1].GetString());
  EXPECT_STREQ("vmos", process_header[2].GetString());
  auto process = processes[1].GetArray();
  EXPECT_EQ(100U, process[0].GetUint64());
  EXPECT_STREQ("p1", process[1].GetString());
  auto process_vmos = process[2].GetArray();
  ASSERT_EQ(1U, process_vmos.Size());
  EXPECT_EQ(1, process_vmos[0]);

  auto vmo_names = capture["VmoNames"].GetArray();
  ASSERT_EQ(1U, vmo_names.Size());
  EXPECT_STREQ("v1", vmo_names[0].GetString());

  auto vmos = capture["Vmos"].GetArray();
  ASSERT_EQ(2U, vmos.Size());
  auto vmo_header = vmos[0].GetArray();
  EXPECT_STREQ("koid", vmo_header[0].GetString());
  EXPECT_STREQ("name", vmo_header[1].GetString());
  EXPECT_STREQ("parent_koid", vmo_header[2].GetString());
  EXPECT_STREQ("committed_bytes", vmo_header[3].GetString());
  EXPECT_STREQ("allocated_bytes", vmo_header[4].GetString());
  auto vmo = vmos[1].GetArray();
  EXPECT_EQ(1U, vmo[0].GetUint64());
  EXPECT_EQ(0U, vmo[1].GetUint64());
  EXPECT_EQ(100U, vmo[2].GetUint64());
  EXPECT_EQ(200U, vmo[3].GetUint64());
  EXPECT_EQ(300U, vmo[4].GetUint64());

  // Test the content of `buckets`.
  ASSERT_TRUE(buckets.IsArray());
  EXPECT_EQ(1U, buckets.Size());
  rapidjson::Value const& bucket = buckets[0];
  EXPECT_EQ(29U, bucket["event_code"].GetUint64());
  EXPECT_STREQ("BlobfsInactive", bucket["name"].GetString());
  EXPECT_STREQ("blobfs\\.cm", bucket["process"].GetString());
  EXPECT_STREQ("inactive-blob-.*", bucket["vmo"].GetString());
}

TEST_F(PrinterUnitTest, PrintSummaryKMEM) {
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .time = 1234,
                                   .kmem =
                                       {
                                           .total_bytes = 1024 * 1024,
                                           .free_bytes = 1024,
                                           .wired_bytes = 2 * 1024,
                                           .total_heap_bytes = 3 * 1024,
                                           .free_heap_bytes = 2 * 1024,
                                           .vmo_bytes = 5 * 1024,
                                           .mmu_overhead_bytes = 6 * 1024,
                                           .ipc_bytes = 7 * 1024,
                                           .other_bytes = 8 * 1024,
                                       },
                               });

  std::ostringstream oss;
  Printer p(oss);
  Summary s(c);
  p.PrintSummary(s, KMEM, SORTED);

  ConfirmLines(oss, {
                        "Time: 1234 VMO: 5K Free: 1K",
                    });
}

TEST_F(PrinterUnitTest, PrintSummaryPROCESS) {
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .time = 1234,
                                   .kmem =
                                       {
                                           .total_bytes = 1024 * 1024,
                                           .free_bytes = 1024,
                                           .wired_bytes = 2 * 1024,
                                           .total_heap_bytes = 3 * 1024,
                                           .free_heap_bytes = 2 * 1024,
                                           .vmo_bytes = 5 * 1024,
                                           .mmu_overhead_bytes = 6 * 1024,
                                           .ipc_bytes = 7 * 1024,
                                           .other_bytes = 8 * 1024,
                                       },
                                   .vmos = {{.koid = 1, .name = "v1", .committed_bytes = 1024}},
                                   .processes = {{.koid = 100, .name = "p1", .vmos = {1}}},
                               });

  std::ostringstream oss;
  Printer p(oss);
  Summary s(c);
  p.PrintSummary(s, PROCESS, SORTED);

  ConfirmLines(oss, {
                        "Time: 1234 VMO: 5K Free: 1K",
                        "kernel<1> 30K",
                        "p1<100> 1K",
                    });
}

TEST_F(PrinterUnitTest, PrintSummaryVMO) {
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .time = 1234,
                                   .kmem =
                                       {
                                           .total_bytes = 1024 * 1024,
                                           .free_bytes = 1024,
                                           .wired_bytes = 2 * 1024,
                                           .total_heap_bytes = 3 * 1024,
                                           .free_heap_bytes = 2 * 1024,
                                           .vmo_bytes = 5 * 1024,
                                           .mmu_overhead_bytes = 6 * 1024,
                                           .ipc_bytes = 7 * 1024,
                                           .other_bytes = 8 * 1024,
                                       },
                                   .vmos = {{.koid = 1, .name = "v1", .committed_bytes = 1024}},
                                   .processes = {{.koid = 100, .name = "p1", .vmos = {1}}},
                               });

  std::ostringstream oss;
  Printer p(oss);
  Summary s(c);
  p.PrintSummary(s, VMO, SORTED);

  ConfirmLines(oss, {
                        "Time: 1234 VMO: 5K Free: 1K",
                        "kernel<1> 30K",
                        " other 8K",
                        " ipc 7K",
                        " mmu 6K",
                        " vmo 4K",
                        " heap 3K",
                        " wired 2K",
                        "p1<100> 1K",
                        " v1 1K",
                    });
}

TEST_F(PrinterUnitTest, PrintSummaryVMOShared) {
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .time = 1234,
                                   .kmem = {.vmo_bytes = 6 * 1024},
                                   .vmos =
                                       {
                                           {.koid = 1, .name = "v1", .committed_bytes = 1024},
                                           {.koid = 2, .name = "v2", .committed_bytes = 2 * 1024},
                                           {.koid = 3, .name = "v3", .committed_bytes = 3 * 1024},
                                       },
                                   .processes =
                                       {
                                           {.koid = 100, .name = "p1", .vmos = {1, 2}},
                                           {.koid = 200, .name = "p2", .vmos = {2, 3}},
                                       },
                               });

  std::ostringstream oss;
  Printer p(oss);
  Summary s(c);
  p.PrintSummary(s, VMO, SORTED);

  ConfirmLines(oss, {
                        "Time: 1234 VMO: 6K Free: 0B",
                        "p2<200> 3K 4K 5K",
                        " v3 3K",
                        " v2 0B 1K 2K",
                        "p1<100> 1K 2K 3K",
                        " v1 1K",
                        " v2 0B 1K 2K",
                        "kernel<1> 0B",
                    });
}

TEST_F(PrinterUnitTest, OutputSummarySingle) {
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .time = 1234L * 1000000000L,
                                   .vmos =
                                       {
                                           {.koid = 1, .name = "v1", .committed_bytes = 100},
                                       },
                                   .processes =
                                       {
                                           {.koid = 100, .name = "p1", .vmos = {1}},
                                       },
                               });
  Summary s(c);

  std::ostringstream oss;
  Printer p(oss);

  p.OutputSummary(s, SORTED, ZX_KOID_INVALID);
  ConfirmLines(oss, {
                        "1234,100,p1,100,100,100",
                        "1234,1,kernel,0,0,0",
                    });

  oss.str("");
  p.OutputSummary(s, SORTED, 100);
  ConfirmLines(oss, {
                        "1234,100,v1,100,100,100",
                    });
}

TEST_F(PrinterUnitTest, OutputSummaryKernel) {
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .time = 1234L * 1000000000L,
                                   .kmem =
                                       {
                                           .wired_bytes = 10,
                                           .total_heap_bytes = 20,
                                           .vmo_bytes = 60,
                                           .mmu_overhead_bytes = 30,
                                           .ipc_bytes = 40,
                                           .other_bytes = 50,
                                       },
                               });
  Summary s(c);

  std::ostringstream oss;
  Printer p(oss);

  p.OutputSummary(s, SORTED, ZX_KOID_INVALID);
  ConfirmLines(oss, {
                        "1234,1,kernel,210,210,210",
                    });

  oss.str("");
  p.OutputSummary(s, SORTED, ProcessSummary::kKernelKoid);
  ConfirmLines(oss, {
                        "1234,1,vmo,60,60,60",
                        "1234,1,other,50,50,50",
                        "1234,1,ipc,40,40,40",
                        "1234,1,mmu,30,30,30",
                        "1234,1,heap,20,20,20",
                        "1234,1,wired,10,10,10",
                    });
}

TEST_F(PrinterUnitTest, OutputSummaryDouble) {
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .time = 1234L * 1000000000L,
                                   .vmos =
                                       {
                                           {.koid = 1, .name = "v1", .committed_bytes = 100},
                                           {.koid = 2, .name = "v2", .committed_bytes = 200},
                                       },
                                   .processes =
                                       {
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
                        "1234,1,kernel,0,0,0",
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
  TestUtils::CreateCapture(&c, {
                                   .time = 1234L * 1000000000L,
                                   .vmos =
                                       {
                                           {.koid = 1, .name = "v1", .committed_bytes = 100},
                                           {.koid = 2, .name = "v1", .committed_bytes = 100},
                                           {.koid = 3, .name = "v1", .committed_bytes = 100},
                                           {.koid = 4, .name = "v2", .committed_bytes = 100},
                                           {.koid = 5, .name = "v3", .committed_bytes = 200},
                                       },
                                   .processes =
                                       {
                                           {.koid = 100, .name = "p1", .vmos = {1, 2, 4}},
                                           {.koid = 200, .name = "p2", .vmos = {2, 3, 5}},
                                       },
                               });
  Summary s(c);

  std::ostringstream oss;
  Printer p(oss);

  p.OutputSummary(s, SORTED, ZX_KOID_INVALID);
  ConfirmLines(oss, {
                        "1234,200,p2,300,350,400",
                        "1234,100,p1,200,250,300",
                        "1234,1,kernel,0,0,0",
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

TEST_F(PrinterUnitTest, PrintDigest) {
  // Test kernel stats.
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .kmem =
                                       {
                                           .total_bytes = 1000,
                                           .free_bytes = 100,
                                           .wired_bytes = 10,
                                           .vmo_bytes = 700,
                                       },
                                   .vmos =
                                       {
                                           {.koid = 1, .name = "a1", .committed_bytes = 100},
                                           {.koid = 2, .name = "b1", .committed_bytes = 200},
                                           {.koid = 3, .name = "c1", .committed_bytes = 300},
                                       },
                                   .processes =
                                       {
                                           {.koid = 1, .name = "p1", .vmos = {1}},
                                           {.koid = 2, .name = "q1", .vmos = {2}},
                                       },
                               });
  Digester digester({{"A", ".*", "a.*"}, {"B", ".*", "b.*"}});
  Digest d(c, &digester);
  std::ostringstream oss;
  Printer p(oss);
  p.PrintDigest(d);
  ConfirmLines(oss, {"B: 200B", "A: 100B", "Undigested: 300B", "Orphaned: 100B", "Kernel: 10B",
                     "Free: 100B"});
}

TEST_F(PrinterUnitTest, OutputDigest) {
  // Test kernel stats.
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .time = 1234L * 1000000000L,
                                   .kmem =
                                       {
                                           .total_bytes = 1000,
                                           .free_bytes = 100,
                                           .wired_bytes = 10,
                                           .vmo_bytes = 700,
                                       },
                                   .kmem_extended =
                                       {
                                           .total_bytes = 1000,
                                           .free_bytes = 100,
                                           .wired_bytes = 10,
                                           .vmo_bytes = 700,
                                           .vmo_pager_total_bytes = 300,
                                           .vmo_pager_newest_bytes = 50,
                                           .vmo_pager_oldest_bytes = 150,
                                           .vmo_discardable_locked_bytes = 60,
                                           .vmo_discardable_unlocked_bytes = 40,
                                       },
                                   .vmos =
                                       {
                                           {.koid = 1, .name = "a1", .committed_bytes = 100},
                                           {.koid = 2, .name = "b1", .committed_bytes = 200},
                                           {.koid = 3, .name = "c1", .committed_bytes = 300},
                                       },
                                   .processes =
                                       {
                                           {.koid = 1, .name = "p1", .vmos = {1}},
                                           {.koid = 2, .name = "q1", .vmos = {2}},
                                       },
                               });
  Digester digester({{"A", ".*", "a.*"}, {"B", ".*", "b.*"}});
  Digest d(c, &digester);
  std::ostringstream oss;
  Printer p(oss);
  p.OutputDigest(d);
  ConfirmLines(oss, {"1234,B,200", "1234,A,100", "1234,Undigested,300", "1234,Orphaned,100",
                     "1234,Kernel,10", "1234,Free,100", "1234,[Addl]PagerTotal,300",
                     "1234,[Addl]PagerNewest,50", "1234,[Addl]PagerOldest,150",
                     "1234,[Addl]DiscardableLocked,60", "1234,[Addl]DiscardableUnlocked,40"});
}

TEST_F(PrinterUnitTest, FormatSize) {
  struct TestCase {
    uint64_t bytes;
    const char* val;
  };
  std::vector<TestCase> tests = {
      {0, "0B"},
      {1, "1B"},
      {1023, "1023B"},
      {1024, "1K"},
      {1025, "1K"},
      {1029, "1K"},
      {1124, "1.1K"},
      {1536, "1.5K"},
      {2047, "2K"},
      {1024 * 1024, "1M"},
      {1024 * 1024 * 1024, "1G"},
      {1024UL * 1024 * 1024 * 1024, "1T"},
      {1024UL * 1024 * 1024 * 1024 * 1024, "1P"},
      {1024UL * 1024 * 1024 * 1024 * 1024 * 1024, "1E"},
      {1024UL * 1024 * 1024 * 1024 * 1024 * 1024 * 1024, "0B"},
  };
  for (const auto& test : tests) {
    char buf[kMaxFormattedStringSize];
    EXPECT_STREQ(test.val, FormatSize(test.bytes, buf));
  }
}

}  // namespace test
}  // namespace memory
