// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_ISP_MODULES_ISP_BLOCK_REGISTERS_H_
#define SRC_CAMERA_DRIVERS_ISP_MODULES_ISP_BLOCK_REGISTERS_H_

namespace camera {

// This class is meant to form the base class for grouping isp registers
// logically. More may be added to this class as development continues.
// |RegisterDefs| should be a struct grouping the registers for a specific block
// together. For example, say there is some DigitalGain block with the following
// tunable settings: Enable, Gain,
//
//    class DigitalGain_Enable : public hwreg::RegisterBase<...>
//    class DigitalGain_Gain : public hwreg::RegisterBase<...>
//
// Logically, these two registers are related and would be grouped:
//
//    struct DGRegisterDefs {
//      DigitalGain_Enable enable;
//      DigitalGain_Gain gain;
//    }
//
// Then |IspBlockRegisters| would be subclassed as follows:
//
//    class DigitalGainRegisters : public IspBlockRegisters<DGRegisterDefs> {
//      ...
//    }
template <typename RegisterDefs>
class IspBlockRegisters {
 public:
  IspBlockRegisters(ddk::MmioView mmio_local, RegisterDefs register_defs)
      : mmio_local_(mmio_local), register_defs_(register_defs) {}

  virtual ~IspBlockRegisters() {}

  virtual void Init() { WriteRegisters(); }

  virtual void WriteRegisters() = 0;

 protected:
  ddk::MmioView mmio_local_;
  RegisterDefs register_defs_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_ISP_MODULES_ISP_BLOCK_REGISTERS_H_
