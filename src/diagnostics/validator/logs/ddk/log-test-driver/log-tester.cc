// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "log-tester.h"

#include <assert.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/process.h>

#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#include "src/diagnostics/validator/logs/ddk/log-test-driver/log-test-bind.h"

namespace log_test_driver {

zx_status_t LogTester::Create(zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<LogTester>(&ac, parent);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto status = dev->Init();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();
  return ZX_OK;
}

zx_status_t LogTester::Init() { return DdkAdd("virtual-logsink", DEVICE_ADD_NON_BINDABLE); }

void LogTester::DdkInit(ddk::InitTxn txn) {
  zxlogf(INFO, "Puppet started.");
  return txn.Reply(ZX_OK);
}

void LogTester::EmitPrintfLog(EmitPrintfLogRequestView request,
                              EmitPrintfLogCompleter::Sync& completer) {
  completer.Reply();
}

void LogTester::StopInterestListener(StopInterestListenerRequestView request,
                                     StopInterestListenerCompleter::Sync& completer) {
  completer.Reply();
}

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

void LogTester::GetInfo(GetInfoRequestView request, GetInfoCompleter::Sync& completer) {
  fuchsia_validate_logs::wire::PuppetInfo info;
  info.pid = GetKoid(zx_process_self());
  info.tid = GetKoid(zx_thread_self());
  completer.Reply(std::move(info));
}

void LogTester::EmitLog(EmitLogRequestView request, EmitLogCompleter::Sync& completer) {
  using fuchsia_diagnostics::wire::Severity;
  fx_log_severity_t severity;
  switch (request->spec.record.severity) {
    case Severity::kTrace:
      severity = DDK_LOG_TRACE;
      break;
    case Severity::kDebug:
      zxlogf(INFO, "Got a request to log at debug level -- this would do nothing.");
      severity = DDK_LOG_DEBUG;
      break;
    case Severity::kInfo:
      severity = DDK_LOG_INFO;
      break;
    case Severity::kWarn:
      severity = DDK_LOG_WARNING;
      break;
    case Severity::kError:
      severity = DDK_LOG_ERROR;
      break;
    case Severity::kFatal:
      // DDK doesn't appear to support FATAL logs.
      abort();
      break;
    default:
      abort();
  }
  auto& txt = request->spec.record.arguments.at(0).value.text();
  std::string cpp_str(txt.begin(), txt.end());
  driver_logf_internal(__zircon_driver_rec__.driver, severity, nullptr, request->spec.file.data(),
                       request->spec.line, "%s\n", cpp_str.c_str());
  completer.Reply();
}

void LogTester::DdkRelease() { delete this; }

static zx_status_t log_test_driver_bind(void* ctx, zx_device_t* parent) {
  return log_test_driver::LogTester::Create(parent);
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = log_test_driver_bind;
  return ops;
}();

}  // namespace log_test_driver

ZIRCON_DRIVER(log_test_driver, log_test_driver::driver_ops, "zircon", "0.1");
