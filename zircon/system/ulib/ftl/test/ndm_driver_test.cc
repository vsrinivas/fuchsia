// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "ftl_private.h"
#include "lib/ftl/ndm-driver.h"

namespace {

class MockDriver final : public ftl::NdmBaseDriver {
 public:
  MockDriver() : NdmBaseDriver(ftl::DefaultLogger()) {}
  ~MockDriver() final {}

  void set_incomplete(bool value) { incomplete_ = value; }
  void set_result(int result) { result_ = result; }
  void set_empty(bool value) { empty_ = value; }

  void GetNdmDriver(NDMDrvr* driver) { FillNdmDriver({}, true, driver); }

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
  bool IncompletePageWrite(uint8_t* spare, uint8_t* data) final { return incomplete_; }
  uint32_t PageSize() final { return 4096; }
  uint8_t SpareSize() final { return 16; }

 private:
  int result_ = ftl::kNdmOk;
  bool empty_ = true;
  bool incomplete_ = false;
};

int MockDriver::NandRead(uint32_t start_page, uint32_t page_count, void* page_buffer,
                         void* oob_buffer) {
  return result_;
}

int MockDriver::NandWrite(uint32_t start_page, uint32_t page_count, const void* page_buffer,
                          const void* oob_buffer) {
  return result_;
}

int MockDriver::NandErase(uint32_t page_num) { return result_; }

bool MockDriver::IsEmptyPage(uint32_t page_num, const uint8_t* data, const uint8_t* spare) {
  return empty_;
}

class NdmDriverTest : public ::testing::Test {
 public:
  void SetUp() override {
    driver.GetNdmDriver(&ndm);
    ASSERT_NE(nullptr, ndm.data_and_spare_check);
    ASSERT_NE(nullptr, ndm.read_decode_spare);
    ASSERT_NE(nullptr, ndm.read_spare);

    if (!(ndm.flags & FSF_FREE_SPARE_ECC)) {
      // The current NDM driver has "free" decoding of the spare area, so we use the same callback
      // for read_spare and read_decode_spare and set FSF_FREE_SPARE_ECC. If this flag is ever
      // unset, these callbacks should be updated to prevent any potential read amplification.
      ASSERT_NE(ndm.read_spare, ndm.read_decode_spare)
          << "read_spare and read_decode_spare should have different callbacks if "
             "FSF_FREE_SPARE_ECC is unset (see NdmBaseDriver::FillNdmDriver for details).";
    }
  }

  MockDriver driver;
  NDMDrvr ndm;
};

TEST_F(NdmDriverTest, CheckPageEccError) {
  driver.set_result(ftl::kNdmUncorrectableEcc);
  int status = -1;
  EXPECT_EQ(ftl::kNdmOk, ndm.data_and_spare_check(0, nullptr, nullptr, &status, &driver));
  EXPECT_EQ(NDM_PAGE_INVALID, status);
}

TEST_F(NdmDriverTest, CheckPageFatalError) {
  driver.set_result(ftl::kNdmFatalError);
  int status = -1;
  EXPECT_EQ(ftl::kNdmFatalError, ndm.data_and_spare_check(0, nullptr, nullptr, &status, &driver));
  // Status should not be used in this case, but we check that it was populated regardless to avoid
  // any erroneous misinterpretation of the original value.
  EXPECT_EQ(NDM_PAGE_INVALID, status);
}

TEST_F(NdmDriverTest, CheckPageEmpty) {
  int status = -1;
  EXPECT_EQ(ftl::kNdmOk, ndm.data_and_spare_check(0, nullptr, nullptr, &status, &driver));
  EXPECT_EQ(NDM_PAGE_ERASED, status);
}

TEST_F(NdmDriverTest, CheckPageValid) {
  driver.set_result(ftl::kNdmUnsafeEcc);
  driver.set_empty(false);
  int status = -1;
  EXPECT_EQ(ftl::kNdmOk, ndm.data_and_spare_check(0, nullptr, nullptr, &status, &driver));
  EXPECT_EQ(NDM_PAGE_VALID, status);
}

TEST_F(NdmDriverTest, CheckPageValidIncompleteWrite) {
  driver.set_result(ftl::kNdmUnsafeEcc);
  driver.set_incomplete(true);
  int status = -1;
  EXPECT_EQ(ftl::kNdmOk, ndm.data_and_spare_check(0, nullptr, nullptr, &status, &driver));
  EXPECT_EQ(NDM_PAGE_INVALID, status);
}

TEST_F(NdmDriverTest, ReadSpareFatalError) {
  driver.set_result(ftl::kNdmFatalError);
  EXPECT_EQ(ftl::kNdmFatalError, ndm.read_decode_spare(0, nullptr, &driver));
}

TEST_F(NdmDriverTest, ReadSpareEccError) {
  driver.set_result(ftl::kNdmUncorrectableEcc);
  EXPECT_EQ(ftl::kNdmError, ndm.read_decode_spare(0, nullptr, &driver));
}

TEST_F(NdmDriverTest, ReadSpareUnsafeEcc) {
  driver.set_result(ftl::kNdmUnsafeEcc);
  EXPECT_EQ(ftl::kNdmOk, ndm.read_decode_spare(0, nullptr, &driver));
}

}  // namespace
