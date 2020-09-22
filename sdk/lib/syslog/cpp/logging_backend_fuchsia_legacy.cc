// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/log_level.h>
#include <lib/syslog/cpp/logging_backend.h>
#include <lib/syslog/global.h>
#include <lib/zx/process.h>

#include <iostream>
#include <sstream>

static_assert(syslog::LOG_TRACE == FX_LOG_TRACE);
static_assert(syslog::LOG_DEBUG == FX_LOG_DEBUG);
static_assert(syslog::LOG_INFO == FX_LOG_INFO);
static_assert(syslog::LOG_WARNING == FX_LOG_WARNING);
static_assert(syslog::LOG_ERROR == FX_LOG_ERROR);
static_assert(syslog::LOG_FATAL == FX_LOG_FATAL);

namespace syslog_backend {

void SetLogSettings(const syslog::LogSettings& settings) {
  char process_name[ZX_MAX_NAME_LEN] = "";
  zx_status_t status =
      zx::process::self()->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
  if (status != ZX_OK)
    process_name[0] = '\0';
  syslog_backend::SetLogSettings(settings, {process_name});
}

void SetLogSettings(const syslog::LogSettings& settings,
                    const std::initializer_list<std::string>& tags) {
  const char* ctags[FX_LOG_MAX_TAGS];
  size_t num_tags = 0;
  for (auto& tag : tags) {
    ctags[num_tags++] = tag.c_str();
    if (num_tags >= FX_LOG_MAX_TAGS)
      break;
  }
  int fd = -1;
  if (!settings.log_file.empty()) {
    fd = open(settings.log_file.c_str(), O_WRONLY | O_CREAT | O_APPEND);
    if (fd < 0) {
      fd = -1;
    }
  }
  fx_logger_config_t config = {.min_severity = settings.min_log_level,
                               .console_fd = fd,
                               .log_service_channel = ZX_HANDLE_INVALID,
                               .tags = ctags,
                               .num_tags = num_tags};
  fx_log_reconfigure(&config);
}

syslog::LogSeverity GetMinLogLevel() {
  return static_cast<syslog::LogSeverity>(fx_logger_get_min_severity(fx_log_get_logger()));
}

void WriteLogValue(syslog::LogSeverity severity, const char* file, unsigned int line,
                   const char* tag, const char* condition, const syslog::LogValue& msg) {
  WriteLog(severity, file, line, tag, condition, msg.ToString());
}

void WriteLog(syslog::LogSeverity severity, const char* file, unsigned int line, const char* tag,
              const char* condition, const std::string& msg) {
  std::ostringstream stream;

  stream << "[" << file << "(" << line << ")] ";

  if (condition)
    stream << "Check failed: " << condition << ". ";

  stream << msg;

  // Write fatal logs to stderr as well because death tests sometimes verify a certain
  // log message was printed prior to the crash.
  // TODO(samans): Convert tests to not depend on stderr. https://fxbug.dev/49593
  if (severity == syslog::LOG_FATAL)
    std::cerr << stream.str() << std::endl;

  fx_logger_t* logger = fx_log_get_logger();
  fx_logger_log(logger, severity, tag, stream.str().c_str());
}

}  // namespace syslog_backend
