#include "../isp-mem-array.h"

#include <new-mock-mmio-reg/new-mock-mmio-reg.h>
#include <zxtest/zxtest.h>

namespace camera {
namespace {

constexpr uint32_t kArraySize = 16;

class IspMemArrayTest : public zxtest::Test {
 public:
  void SetUp() override;

  void TearDown() override;

  void SetExpectations();

 protected:
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_registers_;
  std::unique_ptr<ddk::MmioView> local_mmio_;
  std::vector<uint32_t> expectations_;
};

void IspMemArrayTest::SetUp() { expectations_.reserve(kArraySize); }

void IspMemArrayTest::TearDown() { expectations_.clear(); }

void IspMemArrayTest::SetExpectations() {
  for (uint32_t i = 0; i < kArraySize; ++i) {
    (*mock_registers_.get())[i << 2].ExpectWrite(expectations_[i]);
  }
}

TEST_F(IspMemArrayTest, IspMemArray32) {
  auto reg_array = std::make_unique<ddk_mock::MockMmioReg[]>(kArraySize);
  mock_registers_ =
      std::make_unique<ddk_mock::MockMmioRegRegion>(reg_array.get(), sizeof(uint32_t), kArraySize);
  local_mmio_ = std::make_unique<ddk::MmioView>(mock_registers_->GetMmioBuffer().View(0));
  std::shared_ptr<IspMemArray32> mem_array =
      std::make_shared<IspMemArray32>(*local_mmio_.get(), 0x00, kArraySize);
  for (uint32_t i = 0; i < kArraySize; ++i) {
    (*mem_array.get())[i] = i;
    expectations_.push_back(i);
  }
  SetExpectations();
  mem_array->WriteRegisters();
  std::shared_ptr<const IspMemArray32> const_mem_array =
      std::const_pointer_cast<const IspMemArray32>(mem_array);
  for (uint32_t i = 0; i < kArraySize; ++i) {
    (*mem_array.get())[i] = 2 + i;
    expectations_[i] = 2 + i;
    EXPECT_NE((*mem_array.get())[i], (*const_mem_array.get())[i]);
  }
  SetExpectations();
  mem_array->WriteRegisters();
  for (uint32_t i = 0; i < kArraySize; ++i) {
    EXPECT_EQ((*mem_array.get())[i], (*const_mem_array.get())[i]);
  }
}

}  // namespace
}  // namespace camera
