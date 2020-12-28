// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DIAGNOSTICS_VALIDATOR_LOGS_DDK_LOG_TEST_DRIVER_LOG_TESTER_H_
#define SRC_DIAGNOSTICS_VALIDATOR_LOGS_DDK_LOG_TEST_DRIVER_LOG_TESTER_H_

#include <fuchsia/validate/logs/llcpp/fidl.h>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/macros.h>

namespace log_test_driver {

class LogTester;
using LogTesterType = ddk::Device<LogTester, ddk::Initializable, ddk::Unbindable, ddk::Messageable>;

// This is the main class for the log test driver.
class LogTester : public LogTesterType,
                  public ::llcpp::fuchsia::validate::logs::LogSinkPuppet::Interface,
                  public ddk::EmptyProtocol<ZX_PROTOCOL_VIRTUALBUS_TEST> {
 public:
  explicit LogTester(zx_device_t* parent) : LogTesterType(parent) {}

  static zx_status_t Create(zx_device_t* parent);

  // Device protocol implementation.
  void DdkInit(ddk::InitTxn txn);
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void GetInfo(GetInfoCompleter::Sync& completer) override;
  void EmitLog(::llcpp::fuchsia::validate::logs::RecordSpec spec,
               EmitLogCompleter::Sync& completer) override;
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(LogTester);

  zx_status_t Init();
};

}  // namespace log_test_driver

#endif  // SRC_DIAGNOSTICS_VALIDATOR_LOGS_DDK_LOG_TEST_DRIVER_LOG_TESTER_H_
