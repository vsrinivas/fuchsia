// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DIAGNOSTICS_VALIDATOR_LOGS_DDK_LOG_TEST_DRIVER_LOG_TESTER_H_
#define SRC_DIAGNOSTICS_VALIDATOR_LOGS_DDK_LOG_TEST_DRIVER_LOG_TESTER_H_

#include <fidl/fuchsia.validate.logs/cpp/wire.h>
#include <lib/ddk/device.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/macros.h>

namespace log_test_driver {

class LogTester;
using LogTesterType = ddk::Device<LogTester, ddk::Initializable,
                                  ddk::Messageable<fuchsia_validate_logs::LogSinkPuppet>::Mixin>;

// This is the main class for the log test driver.
class LogTester : public LogTesterType, public ddk::EmptyProtocol<ZX_PROTOCOL_VIRTUALBUS_TEST> {
 public:
  explicit LogTester(zx_device_t* parent) : LogTesterType(parent) {}

  static zx_status_t Create(zx_device_t* parent);

  // Device protocol implementation.
  void DdkInit(ddk::InitTxn txn);
  void GetInfo(GetInfoRequestView request, GetInfoCompleter::Sync& completer) override;
  void EmitLog(EmitLogRequestView request, EmitLogCompleter::Sync& completer) override;
  void EmitPrintfLog(EmitPrintfLogRequestView request,
                     EmitPrintfLogCompleter::Sync& completer) override;
  void StopInterestListener(StopInterestListenerRequestView request,
                            StopInterestListenerCompleter::Sync& completer) override;
  void DdkRelease();

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(LogTester);

  zx_status_t Init();
};

}  // namespace log_test_driver

#endif  // SRC_DIAGNOSTICS_VALIDATOR_LOGS_DDK_LOG_TEST_DRIVER_LOG_TESTER_H_
