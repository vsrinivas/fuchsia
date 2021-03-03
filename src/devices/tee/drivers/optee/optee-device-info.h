// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_DEVICE_INFO_H_
#define SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_DEVICE_INFO_H_

#include <fuchsia/tee/llcpp/fidl.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <tee-client-api/tee-client-types.h>

#include "optee-controller.h"

namespace optee {

// `OpteeDeviceInfo` is a simple child device of `OpteeController` that serves the
// `fuchsia.tee.DeviceInfo` FIDL protocol.

namespace fuchsia_tee = ::fuchsia_tee;

class OpteeDeviceInfo;
using OpteeDeviceInfoBase = ddk::Device<OpteeDeviceInfo, ddk::Messageable>;
using OpteeDeviceInfoProtocol = ddk::EmptyProtocol<ZX_PROTOCOL_TEE>;

class OpteeDeviceInfo : public OpteeDeviceInfoBase,
                        public OpteeDeviceInfoProtocol,
                        public fuchsia_tee::DeviceInfo::Interface {
 public:
  explicit OpteeDeviceInfo(const OpteeController* controller)
      : OpteeDeviceInfoBase(controller->zxdev()), controller_(controller) {}

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease();

  // `DeviceInfo` FIDL protocol
  void GetOsInfo(GetOsInfoCompleter::Sync& completer) override;

 private:
  static constexpr fuchsia_tee::wire::Uuid kOpteeOsUuid = {
      0x486178E0, 0xE7F8, 0x11E3, {0xBC, 0x5E, 0x00, 0x02, 0xA5, 0xD5, 0xC5, 0x1B}};

  const OpteeController* controller_;
};

}  // namespace optee

#endif  // SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_DEVICE_INFO_H_
