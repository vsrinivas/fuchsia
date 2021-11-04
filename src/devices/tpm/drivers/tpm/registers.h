// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TPM_DRIVERS_TPM_REGISTERS_H_
#define SRC_DEVICES_TPM_DRIVERS_TPM_REGISTERS_H_

#include <fidl/fuchsia.hardware.tpmimpl/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/zx/status.h>

#include <hwreg/bitfields.h>

namespace tpm {

using fuchsia_hardware_tpmimpl::wire::RegisterAddress;

enum TpmFamily {
  // TPM v1.2
  kTpmFamily12 = 0,
  // TPM v2.0
  kTpmFamily20 = 1,
};

// This base class just implements convenience |ReadFrom| and |WriteTo| methods that use a supplied
// FIDL client to write/read a TPM register.
template <class SelfType, typename BaseType, uint16_t address>
class TpmReg : public hwreg::RegisterBase<SelfType, BaseType, hwreg::EnablePrinter> {
 public:
  zx_status_t ReadFrom(fidl::WireSyncClient<fuchsia_hardware_tpmimpl::TpmImpl>& client) {
    auto result = client->Read(0, RegisterAddress(address), sizeof(BaseType));
    if (!result.ok()) {
      zxlogf(ERROR, "Failed to send read FIDL request: %s", result.FormatDescription().data());
      return result.status();
    }
    if (result->result.is_err()) {
      zxlogf(ERROR, "Failed to read: %d", result->result.err());
      return result->result.err();
    }
    auto& data = result->result.response().data;
    if (data.count() != sizeof(BaseType)) {
      zxlogf(ERROR, "Incorrect response size");
      return ZX_ERR_BAD_STATE;
    }

    BaseType val = 0;
    memcpy(&val, data.data(), sizeof(val));
    this->set_reg_value(val);
    return ZX_OK;
  }

  zx_status_t WriteTo(fidl::WireSyncClient<fuchsia_hardware_tpmimpl::TpmImpl>& client) {
    auto value = this->reg_value();
    auto data =
        fidl::VectorView<uint8_t>::FromExternal(reinterpret_cast<uint8_t*>(&value), sizeof(value));
    auto result = client->Write(0, RegisterAddress(address), data);
    if (!result.ok()) {
      zxlogf(ERROR, "Failed to send write FIDL request: %s", result.FormatDescription().data());
      return result.status();
    }
    if (result->result.is_err()) {
      zxlogf(ERROR, "Failed to write: %d", result->result.err());
      return result->result.err();
    }
    return ZX_OK;
  }
};

// All of these registers are defined in the TPM PC client platform spec.
// https://www.trustedcomputinggroup.org/wp-content/uploads/PCClientPlatform-TPM-Profile-for-TPM-2-0-v1-03-20-161114_public-review.pdf
// The PC client platform spec defines separate registers for SPI/LPC and I2C, however, the two are
// mostly compatible.

// TPM_STS: 5.5.2.5, "Status Register" and 7.3.5.6, "TPM_STS".
class StsReg : public TpmReg<StsReg, uint32_t, RegisterAddress::kTpmSts> {
 public:
  DEF_ENUM_FIELD(TpmFamily, 27, 26, tpm_family);
  DEF_BIT(25, reset_establishment);
  DEF_BIT(24, command_cancel);
  DEF_FIELD(23, 8, burst_count);
  DEF_BIT(7, sts_valid);
  DEF_BIT(6, command_ready);
  DEF_BIT(5, tpm_go);
  DEF_BIT(4, data_avail);
  DEF_BIT(3, expect);
  DEF_BIT(2, self_test_done);
  DEF_BIT(1, response_retry);
};

// TPM_INTF_CAPABILITY: 5.5.2.7, "Interface Capability" and 7.3.5.5, "TPM_INT_CAPABILITY".
//
// Note that the I2C version of the interface only defines bits 0, 1, 2, and 7.
// Reads of other fields will always return zero.
class IntfCapabilityReg
    : public TpmReg<IntfCapabilityReg, uint32_t, RegisterAddress::kTpmIntCapability> {
 public:
  DEF_FIELD(30, 28, interface_version);
  DEF_FIELD(10, 9, data_transfer_size_support);
  DEF_BIT(8, burst_count_static);
  DEF_BIT(7, command_ready_int_support);
  DEF_BIT(6, interrupt_edge_falling);
  DEF_BIT(5, interrupt_edge_rising);
  DEF_BIT(4, interrupt_level_low);
  DEF_BIT(3, interrupt_level_high);
  DEF_BIT(2, locality_change_int_supported);
  DEF_BIT(1, sts_valid_int_support);
  DEF_BIT(0, data_avail_int_support);
};

// TPM_DID_VID: 5.4.1.1, "DID/VID Register".
class DidVidReg : public TpmReg<DidVidReg, uint32_t, RegisterAddress::kTpmDidVid> {
 public:
  DEF_FIELD(31, 16, device_id);
  DEF_FIELD(15, 0, vendor_id);
};

// TPM_RID: 5.4.1.2, "RID Register".
class RevisionReg : public TpmReg<RevisionReg, uint8_t, RegisterAddress::kTpmRid> {
 public:
  DEF_FIELD(7, 0, revision_id);
};

}  // namespace tpm

#endif  // SRC_DEVICES_TPM_DRIVERS_TPM_REGISTERS_H_
