// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/ndm-driver.h"

#include <zxtest/zxtest.h>

#include "ndm/ndmp.h"
#include "inc/kprivate/ndm.h"

namespace {

class MockDriver final : public ftl::NdmBaseDriver {
 public:
  MockDriver() {}
  ~MockDriver() final {}

  void set_result(int result) { result_ = result; }
  void set_empty(bool value) { empty_ = value; }

  void GetNdmDriver(NDMDrvr* driver) { FillNdmDriver({}, driver); }

  // NdmDriver interface:
  const char* Init() final { return nullptr; }
  const char* Attach(const ftl::Volume* ftl_volume) final { return nullptr; }
  bool Detach() final { return true; }
  int NandRead(uint32_t start_page, uint32_t page_count, void* page_buffer, void* oob_buffer) final;
  int NandWrite(uint32_t start_page, uint32_t page_count, const void* page_buffer,
                const void* oob_buffer) final;
  int NandErase(uint32_t page_num) final;
  int IsBadBlock(uint32_t page_num) final { return ftl::kFalse; }
  bool IsEmptyPage(uint32_t page_num, const uint8_t* data, const uint8_t* spare) final;

 private:
  int result_ = ftl::kNdmOk;
  bool empty_ = true;
};

int MockDriver::NandRead(uint32_t start_page, uint32_t page_count, void* page_buffer,
                           void* oob_buffer) {
  return result_;
}

int MockDriver::NandWrite(uint32_t start_page, uint32_t page_count, const void* page_buffer,
                            const void* oob_buffer) {
  return result_;
}

int MockDriver::NandErase(uint32_t page_num) {
  return result_;
}

bool MockDriver::IsEmptyPage(uint32_t page_num, const uint8_t* data, const uint8_t* spare) {
  return empty_;
}

TEST(NdmDriverTest, CheckPageEccError) {
  MockDriver driver;

  NDMDrvr ndm;
  driver.GetNdmDriver(&ndm);
  ASSERT_NOT_NULL(ndm.data_and_spare_check);

  driver.set_result(ftl::kNdmUncorrectableEcc);

  int status;
  EXPECT_EQ(ftl::kNdmOk, ndm.data_and_spare_check(0, nullptr, nullptr, &status, &driver));
  EXPECT_EQ(NDM_PAGE_INVALID, status);
}

TEST(NdmDriverTest, CheckPageFatalError) {
  MockDriver driver;

  NDMDrvr ndm;
  driver.GetNdmDriver(&ndm);
  ASSERT_NOT_NULL(ndm.data_and_spare_check);

  driver.set_result(ftl::kNdmFatalError);

  int status;
  EXPECT_EQ(ftl::kNdmOk, ndm.data_and_spare_check(0, nullptr, nullptr, &status, &driver));
  EXPECT_EQ(NDM_PAGE_INVALID, status);
}

TEST(NdmDriverTest, CheckPageEmpty) {
  MockDriver driver;

  NDMDrvr ndm;
  driver.GetNdmDriver(&ndm);
  ASSERT_NOT_NULL(ndm.data_and_spare_check);

  int status;
  EXPECT_EQ(ftl::kNdmOk, ndm.data_and_spare_check(0, nullptr, nullptr, &status, &driver));
  EXPECT_EQ(NDM_PAGE_ERASED, status);
}

TEST(NdmDriverTest, CheckPageValid) {
  MockDriver driver;

  NDMDrvr ndm;
  driver.GetNdmDriver(&ndm);
  ASSERT_NOT_NULL(ndm.data_and_spare_check);

  driver.set_result(ftl::kNdmUnsafeEcc);
  driver.set_empty(false);

  int status;
  EXPECT_EQ(ftl::kNdmOk, ndm.data_and_spare_check(0, nullptr, nullptr, &status, &driver));
  EXPECT_EQ(NDM_PAGE_VALID, status);
}

}  // namespace
