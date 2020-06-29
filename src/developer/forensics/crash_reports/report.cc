// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/report.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

namespace forensics {
namespace crash_reports {
namespace {

std::optional<Report::Attachment> MakeAttachment(const fuchsia::mem::Buffer& buffer) {
  if (!buffer.vmo.is_valid()) {
    return std::nullopt;
  }

  auto data = std::make_unique<uint8_t[]>(buffer.size);
  if (const zx_status_t status = buffer.vmo.read(data.get(), /*offset=*/0u, /*len=*/buffer.size);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to read vmo";
    return std::nullopt;
  }
  return Report::Attachment{.data = std::move(data), .size = buffer.size};
}

}  // namespace

std::optional<Report> Report::MakeReport(const std::string& program_shortname,
                                         const std::map<std::string, std::string>& annotations,
                                         std::map<std::string, fuchsia::mem::Buffer> attachments,
                                         std::optional<fuchsia::mem::Buffer> minidump) {
  std::map<std::string, Attachment> attachment_copies;
  for (const auto& [k, v] : attachments) {
    auto attachment = MakeAttachment(v);
    if (!attachment) {
      return std::nullopt;
    }
    attachment_copies.emplace(k, std::move(attachment.value()));
  }

  std::optional<Attachment> minidump_copy =
      minidump.has_value() ? MakeAttachment(minidump.value()) : std::nullopt;

  return Report(program_shortname, annotations, std::move(attachment_copies),
                std::move(minidump_copy));
}

Report::Report(const std::string& program_shortname,
               const std::map<std::string, std::string>& annotations,
               std::map<std::string, Attachment> attachments, std::optional<Attachment> minidump)
    : program_shortname_(program_shortname),
      annotations_(annotations),
      attachments_(std::move(attachments)),
      minidump_(std::move(minidump)) {}

}  // namespace crash_reports
}  // namespace forensics
