#include "isp-mem-array.h"

namespace camera {

uint32_t IspMemArray32::operator[](uint32_t idx) const {
  // Shift to accommodate difference in logical location in array to address.
  // clang-format off
  return hwreg::RegisterAddr<IspMemArray32Reg>(start_addr_ + (idx << 2))
    .ReadFrom(&mmio_)
    .value();
  // clang-format on
}

void IspMemArray32::WriteRegisters() {
  for (uint32_t i = 0; i < data_size_; ++i) {
    hwreg::RegisterAddr<IspMemArray32Reg>(start_addr_ + (i << 2))
        .ReadFrom(&mmio_)
        .set_value(data_[i])
        .WriteTo(&mmio_);
  }
}

}  // namespace camera
