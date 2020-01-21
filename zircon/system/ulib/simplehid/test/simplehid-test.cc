// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/simplehid/simplehid.h"

#include <lib/sync/completion.h>

#include <zxtest/zxtest.h>

namespace simplehid {

namespace {

struct TestReport {
  uint32_t value1 = 0;
  uint32_t value2 = 0;
};

struct TestContext {
  sync_completion_t signal;
  TestReport report;
  size_t report_size = 0;
};

void IoQueueMock(void* ctx, const void* buf_buffer, size_t buf_size, zx_time_t time) {
  TestContext* test = reinterpret_cast<TestContext*>(ctx);

  test->report_size = buf_size;
  if (buf_size == sizeof(test->report)) {
    test->report = *reinterpret_cast<const TestReport*>(buf_buffer);
  }

  sync_completion_signal(&test->signal);
}

}  // namespace

TEST(SimpleHidTest, NoReports) {
  bool called = false;
  fit::function<zx_status_t(TestReport*)> get_input_report = [&called](TestReport* report) {
    called = true;
    report->value1 = 0x3f455860;
    report->value2 = 0xe365dcec;
    return ZX_OK;
  };

  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));

  SimpleHid<TestReport> test(std::move(port), std::move(get_input_report));

  TestContext ctx;
  hidbus_ifc_protocol_ops_t ifc_ops = {IoQueueMock};
  hidbus_ifc_protocol_t ifc = {&ifc_ops, &ctx};

  ASSERT_OK(test.HidbusStart(&ifc));
  ASSERT_OK(test.SetReportInterval(0));

  ASSERT_STATUS(ZX_ERR_TIMED_OUT, sync_completion_wait(&ctx.signal, ZX_SEC(3)));

  test.HidbusStop();

  EXPECT_FALSE(called);
  EXPECT_EQ(0, test.GetReportInterval());
}

TEST(SimpleHidTest, Reports) {
  constexpr uint32_t kReportValue1 = 0x3f455860;
  constexpr uint32_t kReportValue2 = 0xe365dcec;

  bool called = false;
  fit::function<zx_status_t(TestReport*)> get_input_report = [&called](TestReport* report) {
    called = true;
    report->value1 = kReportValue1;
    report->value2 = kReportValue2;
    return ZX_OK;
  };

  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));

  SimpleHid<TestReport> test(std::move(port), std::move(get_input_report));

  TestContext ctx;
  hidbus_ifc_protocol_ops_t ifc_ops = {IoQueueMock};
  hidbus_ifc_protocol_t ifc = {&ifc_ops, &ctx};

  ASSERT_OK(test.HidbusStart(&ifc));
  ASSERT_OK(test.SetReportInterval(1000));

  ASSERT_OK(sync_completion_wait(&ctx.signal, ZX_SEC(3)));

  test.HidbusStop();

  EXPECT_TRUE(called);
  EXPECT_EQ(sizeof(ctx.report), ctx.report_size);
  EXPECT_EQ(1000, test.GetReportInterval());
  EXPECT_EQ(kReportValue1, ctx.report.value1);
  EXPECT_EQ(kReportValue2, ctx.report.value2);
}

}  // namespace simplehid
