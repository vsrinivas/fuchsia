// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/report.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

namespace forensics {
namespace crash_reports {
namespace {

std::optional<SizedData> MakeAttachment(const fuchsia::mem::Buffer& buffer) {
  if (!buffer.vmo.is_valid()) {
    return std::nullopt;
  }

  SizedData data;
  data.reserve(buffer.size);
  if (const zx_status_t status = buffer.vmo.read(data.data(), /*offset=*/0u, /*len=*/data.size());
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to read vmo";
    return std::nullopt;
  }
  return data;
}

}  // namespace

std::optional<Report> Report::MakeReport(const std::string& program_shortname,
                                         const std::map<std::string, std::string>& annotations,
                                         std::map<std::string, fuchsia::mem::Buffer> attachments,
                                         std::optional<fuchsia::mem::Buffer> minidump) {
  std::map<std::string, SizedData> attachment_copies;
  for (const auto& [k, v] : attachments) {
    auto attachment = MakeAttachment(v);
    if (!attachment) {
      return std::nullopt;
    }
    attachment_copies.emplace(k, std::move(attachment.value()));
  }

  std::optional<SizedData> minidump_copy =
      minidump.has_value() ? MakeAttachment(minidump.value()) : std::nullopt;

  return Report(program_shortname, annotations, std::move(attachment_copies),
                std::move(minidump_copy));
}

Report::Report(const std::string& program_shortname,
               const std::map<std::string, std::string>& annotations,
               std::map<std::string, SizedData> attachments, std::optional<SizedData> minidump)
    : program_shortname_(program_shortname),
      annotations_(annotations),
      attachments_(std::move(attachments)),
      minidump_(std::move(minidump)) {}

}  // namespace crash_reports
}  // namespace forensics
