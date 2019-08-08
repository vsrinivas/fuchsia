// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/crashpad_report_util.h"

#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

#include <memory>

namespace fuchsia {
namespace crash {

bool WriteVMO(const fuchsia::mem::Buffer& vmo, crashpad::FileWriter* writer) {
  // TODO(frousseau): make crashpad::FileWriter VMO-aware.
  auto data = std::make_unique<uint8_t[]>(vmo.size);
  if (zx_status_t status = vmo.vmo.read(data.get(), 0u, vmo.size); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "failed to read VMO";
    return false;
  }
  return writer->Write(data.get(), vmo.size);
}

bool AddAttachment(const std::string& attachment_filename,
                   const fuchsia::mem::Buffer& attachment_content,
                   crashpad::CrashReportDatabase::NewReport* crashpad_report) {
  crashpad::FileWriter* writer = crashpad_report->AddAttachment(attachment_filename);
  if (!writer) {
    return false;
  }
  if (!WriteVMO(attachment_content, writer)) {
    FX_LOGS(ERROR) << "error writing " << attachment_filename << " to file";
    return false;
  }
  return true;
}

}  // namespace crash
}  // namespace fuchsia
