// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/stream.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <sstream>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/pointer.h>
#include <src/lib/diagnostics/accessor2logger/log_message.h>
#include <src/lib/fsl/vmo/strings.h>

using fuchsia::diagnostics::FormattedContent;
using fuchsia::logger::LogMessage;

namespace diagnostics::accessor2logger {

namespace {
const char kPidLabel[] = "pid";
const char kTidLabel[] = "tid";
const char kFileLabel[] = "file";
const char kLineLabel[] = "line";
const char kTagsLabel[] = "tags";
const char kMessageLabel[] = "value";
const char kVerbosityLabel[] = "verbosity";

inline fuchsia::logger::LogLevelFilter StringToSeverity(const std::string& input) {
  if (strcasecmp(input.c_str(), "trace") == 0) {
    return fuchsia::logger::LogLevelFilter::TRACE;
  }
  if (strcasecmp(input.c_str(), "debug") == 0) {
    return fuchsia::logger::LogLevelFilter::DEBUG;
  }
  if (strcasecmp(input.c_str(), "info") == 0) {
    return fuchsia::logger::LogLevelFilter::INFO;
  }
  if (strcasecmp(input.c_str(), "warn") == 0) {
    return fuchsia::logger::LogLevelFilter::WARN;
  }
  if (strcasecmp(input.c_str(), "error") == 0) {
    return fuchsia::logger::LogLevelFilter::ERROR;
  }
  if (strcasecmp(input.c_str(), "fatal") == 0) {
    return fuchsia::logger::LogLevelFilter::FATAL;
  }

  return fuchsia::logger::LOG_LEVEL_DEFAULT;
}

inline fpromise::result<LogMessage, std::string> JsonToHostLogMessage(rapidjson::Value& value) {
  LogMessage ret = {};
  std::stringstream kv_mapping;

  if (!value.IsObject()) {
    return fpromise::error("Value is not an object");
  }

  auto metadata = value.FindMember("metadata");
  auto payload = value.FindMember("payload");

  if (metadata == value.MemberEnd() || payload == value.MemberEnd() ||
      !metadata->value.IsObject() || !payload->value.IsObject()) {
    return fpromise::error("Expected metadata and payload objects");
  }
  auto root = payload->value.FindMember("root");
  if (!root->value.IsObject()) {
    return fpromise::error("Expected payload.root to be an object if present");
  }

  auto timestamp = metadata->value.FindMember("timestamp");
  if (timestamp == metadata->value.MemberEnd() || !timestamp->value.IsUint64()) {
    return fpromise::error("Expected metadata.timestamp key");
  }
  ret.time = timestamp->value.GetUint64();

  auto severity = metadata->value.FindMember("severity");
  if (severity == metadata->value.MemberEnd() || !severity->value.IsString()) {
    return fpromise::error("Expected metadata.severity key");
  }
  ret.severity = static_cast<int32_t>(StringToSeverity(severity->value.GetString()));

  auto moniker = value.FindMember("moniker");
  std::string moniker_string;
  if (moniker != value.MemberEnd() && moniker->value.IsString()) {
    moniker_string = std::move(moniker->value.GetString());
  }

  uint32_t dropped_logs = 0;
  if (metadata->value.HasMember("errors")) {
    auto& errors = metadata->value["errors"];
    if (errors.IsArray()) {
      for (rapidjson::SizeType i = 0; i < errors.Size(); i++) {
        auto* val = rapidjson::Pointer("/dropped_logs/count").Get(errors[i]);
        if (val && val->IsUint()) {
          dropped_logs += val->GetUint();
        }
      }
    }
  }

  // Flatten payloads containing a "root" node.
  // TODO(fxbug.dev/63409): Remove this when "root" is omitted from logs.
  if (payload->value.MemberCount() == 1 && payload->value.HasMember("root")) {
    payload = payload->value.FindMember("root");
    if (!payload->value.IsObject()) {
      return fpromise::error("Expected payload.root to be an object if present");
    }
    payload = payload->value.FindMember("message");
    if (!payload->value.IsObject()) {
      return fpromise::error("Expected payload.root.message to be an object if present");
    }
  }
  std::string msg;
  std::string filename;
  std::optional<int> verbosity;
  std::optional<int> line_number;

  for (auto it = payload->value.MemberBegin(); it != payload->value.MemberEnd(); ++it) {
    if (!it->name.IsString()) {
      return fpromise::error("A key is not a string");
    }
    std::string name = it->name.GetString();
    if (name == kMessageLabel && it->value.IsString()) {
      msg = std::move(it->value.GetString());
    }
    if ((name == kVerbosityLabel) && it->value.IsInt()) {
      verbosity = it->value.GetInt();
      if (ret.severity != verbosity.value_or(ret.severity)) {
        ret.severity = GetSeverityFromVerbosity(static_cast<uint8_t>(verbosity.value()));
      }
    }
  }

  for (auto it = metadata->value.MemberBegin(); it != metadata->value.MemberEnd(); ++it) {
    if (!it->name.IsString()) {
      return fpromise::error("A key is not a string");
    }
    std::string name = it->name.GetString();
    if (name == kTagsLabel) {
      if (it->value.IsString()) {
        ret.tags.emplace_back(std::move(it->value.GetString()));
      } else if (it->value.IsArray()) {
        for (rapidjson::SizeType i = 0; i < it->value.Size(); ++i) {
          auto& val = it->value[i];
          if (!val.IsString()) {
            return fpromise::error("Tags array must contain strings");
          }
          ret.tags.emplace_back(std::move(val.GetString()));
        }
      } else {
        return fpromise::error("Tags must be a string or array of strings");
      }
    } else if (name == kTidLabel && it->value.IsUint64()) {
      ret.tid = it->value.GetUint64();
    } else if (name == kPidLabel && it->value.IsUint64()) {
      ret.pid = it->value.GetUint64();
    } else if (name == kFileLabel && it->value.IsString()) {
      filename = it->value.GetString();
    } else if (name == kLineLabel && it->value.IsUint64()) {
      line_number = it->value.GetUint64();
    }
  }

  auto kvps = root->value.FindMember("keys");

  if ((kvps != root->value.MemberEnd()) && kvps->value.IsObject() && (kvps->name == "keys")) {
    for (auto it = kvps->value.MemberBegin(); it != kvps->value.MemberEnd(); ++it) {
      if (!it->name.IsString()) {
        return fpromise::error("A key is not a string");
      }
      std::string name = it->name.GetString();
      // If the name of the field is not a known special field, treat it as a key/value pair and
      // append to the message.
      kv_mapping << " " << std::move(name) << "=";
      if (it->value.IsInt64()) {
        kv_mapping << it->value.GetInt64();
      } else if (it->value.IsUint64()) {
        kv_mapping << it->value.GetUint64();
      } else if (it->value.IsDouble()) {
        kv_mapping << std::to_string(it->value.GetDouble());
      } else if (it->value.IsString()) {
        kv_mapping << "\"";
        auto str_value = it->value.GetString();
        if (strchr(str_value, '"') != nullptr) {
          // Escape quotes in strings per host encoding.
          size_t len = strlen(str_value);
          for (size_t i = 0; i < len; ++i) {
            char c = str_value[i];
            if (c == '"') {
              kv_mapping << '\\';
            }
            kv_mapping << c;
          }
        } else {
          kv_mapping << std::move(it->value.GetString());
        }
        kv_mapping << "\"";
      } else {
        kv_mapping << "<unknown>";
      }
    }
  }

  if (!filename.empty() && line_number.has_value()) {
    std::stringstream enc;
    enc << "[" << filename << "(" << line_number.value() << ")] ";
    ret.msg = enc.str();
  }
  ret.msg += msg;
  bool needs_add = true;

  if (!ret.msg.empty()) {
    if (ret.msg[ret.msg.size() - 1] == ' ') {
      std::string truncated = kv_mapping.str();
      truncated.erase(0, 1);
      ret.msg += truncated;
      needs_add = false;
    }
  }

  if (needs_add) {
    ret.msg += kv_mapping.str();
  }

  // If there are no tags, automatically tag with the component moniker.
  if (ret.tags.size() == 0 && !moniker_string.empty()) {
    ret.tags.emplace_back(std::move(moniker_string));
  }

  if (dropped_logs > 0) {
    ret.dropped_logs = dropped_logs;
  }

  return fpromise::ok(std::move(ret));
}

std::string GetComponentName(const std::string& moniker) {
  size_t pos = moniker.rfind('/');
  if (pos == std::string::npos) {
    return moniker;
  }
  // Monikers should never end in / since / is a special
  // character indicating a component in the topology.
  return moniker.substr(pos + 1);
}

inline fpromise::result<LogMessage, std::string> JsonToLogMessage(rapidjson::Value& value) {
  LogMessage ret = {};
  std::stringstream kv_mapping;

  if (!value.IsObject()) {
    return fpromise::error("Value is not an object");
  }

  auto metadata = value.FindMember("metadata");
  auto payload = value.FindMember("payload");

  if (metadata == value.MemberEnd() || payload == value.MemberEnd() ||
      !metadata->value.IsObject() || !payload->value.IsObject()) {
    return fpromise::error("Expected metadata and payload objects");
  }
  auto root = payload->value.FindMember("root");
  if (!root->value.IsObject()) {
    return fpromise::error("Expected payload.root to be an object if present");
  }

  auto timestamp = metadata->value.FindMember("timestamp");
  if (timestamp == metadata->value.MemberEnd() || !timestamp->value.IsUint64()) {
    return fpromise::error("Expected metadata.timestamp key");
  }
  ret.time = timestamp->value.GetUint64();

  auto severity = metadata->value.FindMember("severity");
  if (severity == metadata->value.MemberEnd() || !severity->value.IsString()) {
    return fpromise::error("Expected metadata.severity key");
  }
  ret.severity = static_cast<int32_t>(StringToSeverity(severity->value.GetString()));

  auto moniker = value.FindMember("moniker");
  std::string moniker_string;
  if (moniker != value.MemberEnd() && moniker->value.IsString()) {
    moniker_string = std::move(moniker->value.GetString());
  }

  uint32_t dropped_logs = 0;
  if (metadata->value.HasMember("errors")) {
    auto& errors = metadata->value["errors"];
    if (errors.IsArray()) {
      for (rapidjson::SizeType i = 0; i < errors.Size(); i++) {
        auto* val = rapidjson::Pointer("/dropped_logs/count").Get(errors[i]);
        if (val && val->IsUint()) {
          dropped_logs += val->GetUint();
        }
      }
    }
  }

  // Flatten payloads containing a "root" node.
  // TODO(fxbug.dev/63409): Remove this when "root" is omitted from logs.
  if (payload->value.MemberCount() == 1 && payload->value.HasMember("root")) {
    payload = payload->value.FindMember("root");
    if (!payload->value.IsObject()) {
      return fpromise::error("Expected payload.root to be an object if present");
    }
    payload = payload->value.FindMember("message");
    if (!payload->value.IsObject()) {
      return fpromise::error("Expected payload.root.message to be an object if present");
    }
  }
  std::string msg;
  std::string filename;
  std::optional<int> line_number;
  for (auto it = payload->value.MemberBegin(); it != payload->value.MemberEnd(); ++it) {
    if (!it->name.IsString()) {
      return fpromise::error("A key is not a string");
    }
    std::string name = it->name.GetString();
    if (name == kMessageLabel && it->value.IsString()) {
      msg = std::move(it->value.GetString());
    }
  }
  for (auto it = metadata->value.MemberBegin(); it != metadata->value.MemberEnd(); ++it) {
    if (!it->name.IsString()) {
      return fpromise::error("A key is not a string");
    }
    std::string name = it->name.GetString();
    if (name == kTagsLabel) {
      if (it->value.IsString()) {
        ret.tags.emplace_back(std::move(it->value.GetString()));
      } else if (it->value.IsArray()) {
        for (rapidjson::SizeType i = 0; i < it->value.Size(); ++i) {
          auto& val = it->value[i];
          if (!val.IsString()) {
            return fpromise::error("Tags array must contain strings");
          }
          ret.tags.emplace_back(std::move(val.GetString()));
        }
      } else {
        return fpromise::error("Tags must be a string or array of strings");
      }
    } else if (name == kTidLabel && it->value.IsUint64()) {
      ret.tid = it->value.GetUint64();
    } else if (name == kPidLabel && it->value.IsUint64()) {
      ret.pid = it->value.GetUint64();
    } else if (name == kFileLabel && it->value.IsString()) {
      filename = it->value.GetString();
    } else if (name == kLineLabel && it->value.IsUint64()) {
      line_number = it->value.GetUint64();
    }
  }
  auto kvps = root->value.FindMember("keys");
  if ((kvps != root->value.MemberEnd()) && kvps->value.IsObject() && (kvps->name == "keys")) {
    for (auto it = kvps->value.MemberBegin(); it != kvps->value.MemberEnd(); ++it) {
      if (!it->name.IsString()) {
        return fpromise::error("A key is not a string");
      }
      std::string name = it->name.GetString();
      // If the name of the field is not a known special field, treat it as a key/value pair and
      // append to the message.
      kv_mapping << " " << std::move(name) << "=";
      if (it->value.IsInt64()) {
        kv_mapping << it->value.GetInt64();
      } else if (it->value.IsUint64()) {
        kv_mapping << it->value.GetUint64();
      } else if (it->value.IsDouble()) {
        kv_mapping << it->value.GetDouble();
      } else if (it->value.IsString()) {
        kv_mapping << std::move(it->value.GetString());
      } else {
        kv_mapping << "<unknown>";
      }
    }
  }
  if (!filename.empty() && line_number.has_value()) {
    std::stringstream enc;
    enc << "[" << filename << "(" << line_number.value() << ")] ";
    ret.msg = enc.str();
  }
  ret.msg += msg;
  ret.msg += kv_mapping.str();

  // If there are no tags, automatically tag with the component name derived from the moniker.
  if (ret.tags.size() == 0 && !moniker_string.empty()) {
    ret.tags.emplace_back(GetComponentName(moniker_string));
  }

  if (dropped_logs > 0) {
    ret.dropped_logs = dropped_logs;
  }

  return fpromise::ok(std::move(ret));
}
}  // namespace

fpromise::result<std::vector<fpromise::result<fuchsia::logger::LogMessage, std::string>>,
                 std::string>
ConvertFormattedContentToLogMessages(FormattedContent content) {
  std::vector<fpromise::result<LogMessage, std::string>> output;

  if (!content.is_json()) {
    // Expecting JSON in all cases.
    return fpromise::error("Expected json content");
  }

  std::string data;
  if (!fsl::StringFromVmo(content.json(), &data)) {
    return fpromise::error("Failed to read string from VMO");
  }
  content.json().vmo.reset();

  rapidjson::Document d;
  d.Parse(std::move(data));
  if (d.HasParseError()) {
    std::string error = "Failed to parse content as JSON. Offset " +
                        std::to_string(d.GetErrorOffset()) + ": " +
                        rapidjson::GetParseError_En(d.GetParseError());
    return fpromise::error(std::move(error));
  }

  if (!d.IsArray()) {
    return fpromise::error("Expected content to contain an array");
  }

  for (rapidjson::SizeType i = 0; i < d.Size(); ++i) {
    output.emplace_back(JsonToLogMessage(d[i]));
  }

  return fpromise::ok(std::move(output));
}

fpromise::result<std::vector<fpromise::result<fuchsia::logger::LogMessage, std::string>>,
                 std::string>
ConvertFormattedContentToHostLogMessages(FormattedContent content) {
  std::vector<fpromise::result<LogMessage, std::string>> output;

  if (!content.is_json()) {
    // Expecting JSON in all cases.
    return fpromise::error("Expected json content");
  }

  std::string data;
  if (!fsl::StringFromVmo(content.json(), &data)) {
    return fpromise::error("Failed to read string from VMO");
  }
  content.json().vmo.reset();

  rapidjson::Document d;
  d.Parse(std::move(data));
  if (d.HasParseError()) {
    std::string error = "Failed to parse content as JSON. Offset " +
                        std::to_string(d.GetErrorOffset()) + ": " +
                        rapidjson::GetParseError_En(d.GetParseError());
    return fpromise::error(std::move(error));
  }

  if (!d.IsArray()) {
    return fpromise::error("Expected content to contain an array");
  }

  for (rapidjson::SizeType i = 0; i < d.Size(); ++i) {
    output.emplace_back(JsonToHostLogMessage(d[i]));
  }

  return fpromise::ok(std::move(output));
}

}  // namespace diagnostics::accessor2logger
