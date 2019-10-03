// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_ISP_MALI_009_ARM_ISP_TEST_H_
#define SRC_CAMERA_DRIVERS_ISP_MALI_009_ARM_ISP_TEST_H_

#include <fuchsia/camera/test/c/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fit/function.h>
#include <zircon/fidl.h>

#include <memory>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/isp.h>
#include <fbl/mutex.h>

#include "global_regs.h"
#include "pingpong_regs.h"
#include "stream_server.h"

namespace camera {
// |ArmIspDeviceTester| is spawned by the driver in |arm-isp.cc|
// This provides the interface provided in fuchsia-camera-test/isp.fidl in
// Zircon.

// Organizes the data from a register dump.
struct ArmIspRegisterDump {
  uint32_t global_config[kGlobalConfigSize];
  uint32_t ping_config[kContextConfigSize];
  uint32_t pong_config[kContextConfigSize];
};

class ArmIspDevice;

class ArmIspDeviceTester;
using IspDeviceTesterType = ddk::Device<ArmIspDeviceTester, ddk::UnbindableDeprecated,
                                        ddk::Messageable>;

class ArmIspDeviceTester : public IspDeviceTesterType,
                           public ddk::EmptyProtocol<ZX_PROTOCOL_ISP_TEST> {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ArmIspDeviceTester);

  explicit ArmIspDeviceTester(ArmIspDevice* isp, zx_device_t* parent)
      : IspDeviceTesterType(parent), isp_(isp) {}

  // On successful creation, |on_isp_unbind| is filled with a pointer to the
  // Disconnect function, so that the ArmIspDevice can notify the
  // ArmIspDeviceTester that it is going away.
  static zx_status_t Create(ArmIspDevice* isp, fit::callback<void()>* on_isp_unbind);

  // Returns the ISP's BTI. Avoids the need to add more friend classes to the ISP.
  zx::bti& GetBti() __TA_REQUIRES(isp_lock_);

  // Methods required by the ddk.
  void DdkRelease();
  void DdkUnbindDeprecated();
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

 private:
  // DDKMessage Helper Functions.
  zx_status_t RunTests(fidl_txn_t* txn);
  // Add a client to the existing primary full-resolution stream.
  zx_status_t CreateStream(zx_handle_t stream, fidl_txn_t* txn);

  // Create the single full-resolution stream for the ISP.
  // Clients can request the interface via CreateStream.
  zx_status_t CreateStreamServer() __TA_REQUIRES(server_lock_);

  // Disconnects this instance from the ArmIspDevice it is testing.
  // This should only be called when the ArmIspDevice is going away, because
  // it makes this class rather useless.
  void Disconnect();

  static constexpr fuchsia_camera_test_IspTester_ops isp_tester_ops = {
      .RunTests = fidl::Binder<ArmIspDeviceTester>::BindMember<&ArmIspDeviceTester::RunTests>,
      .CreateStream =
          fidl::Binder<ArmIspDeviceTester>::BindMember<&ArmIspDeviceTester::CreateStream>,
  };

  // ISP Tests:
  // Test the GetRegisters interface by writing to a register.
  // |report| is updated with the results of the tests this function performs.
  void TestWriteRegister(fuchsia_camera_test_TestReport* report);

  // Test the IspCreateOutputStream by calling it.
  // |report| is updated with the results of the tests this function performs.
  void TestConnectStream(fuchsia_camera_test_TestReport* report);

  // Test the callbacks passed to IspCreateOutputStream by calling them.
  // |report| is updated with the results of the tests this function performs.
  void TestCallbacks(fuchsia_camera_test_TestReport* report);

  // The ArmIspDevice is a parent of the ArmIspDeviceTester.  It will call
  // Disconnect() during its DdkUnbindDeprecated() call, so that isp_ never references an
  // invalid instance. The isp_lock_ ensures that isp_ won't be removed while we
  // are using it.
  fbl::Mutex isp_lock_;
  ArmIspDevice* isp_ __TA_GUARDED(isp_lock_);

  // The StreamServer creates an event loop to handle new frames coming from the ISP.
  // It also serves the Stream interface to any number of clients.
  fbl::Mutex server_lock_;
  std::unique_ptr<camera::StreamServer> server_ __TA_GUARDED(server_lock_);
  output_stream_protocol stream_protocol_;
  output_stream_protocol_ops_t stream_protocol_ops_;

  friend class camera::StreamServer;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_ISP_MALI_009_ARM_ISP_TEST_H_
