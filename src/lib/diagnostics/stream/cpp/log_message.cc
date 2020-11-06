// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/stream.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <sstream>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/pointer.h>
#include <src/lib/diagnostics/stream/cpp/log_message.h>
#include <src/lib/fsl/vmo/strings.h>

using fuchsia::diagnostics::FormattedContent;
using fuchsia::logger::LogMessage;

namespace diagnostics::stream {

namespace {
const char kPidLabel[] = "pid";
const char kTidLabel[] = "tid";
const char kTagLabel[] = "tag";
const char kTagsLabel[] = "tags";
const char kMessageLabel[] = "message";

inline int32_t StringToSeverity(const std::string& input) {
  if (strcasecmp(input.c_str(), "trace") == 0) {
    return static_cast<int32_t>(fuchsia::logger::LogLevelFilter::TRACE);
  } else if (strcasecmp(input.c_str(), "debug") == 0) {
    return static_cast<int32_t>(fuchsia::logger::LogLevelFilter::DEBUG);
  } else if (strcasecmp(input.c_str(), "info") == 0) {
    return static_cast<int32_t>(fuchsia::logger::LogLevelFilter::INFO);
  } else if (strcasecmp(input.c_str(), "warn") == 0) {
    return static_cast<int32_t>(fuchsia::logger::LogLevelFilter::WARN);
  } else if (strcasecmp(input.c_str(), "error") == 0) {
    return static_cast<int32_t>(fuchsia::logger::LogLevelFilter::ERROR);
  } else if (strcasecmp(input.c_str(), "fatal") == 0) {
    return static_cast<int32_t>(fuchsia::logger::LogLevelFilter::FATAL);
  }

  return fuchsia::logger::LOG_LEVEL_DEFAULT;
}

inline fit::result<LogMessage, std::string> JsonToLogMessage(rapidjson::Value& value) {
  LogMessage ret = {};
  std::stringstream kv_mapping;

  if (!value.IsObject()) {
    return fit::error("Value is not an object");
  }

  auto metadata = value.FindMember("metadata");
  auto payload = value.FindMember("payload");

  if (metadata == value.MemberEnd() || payload == value.MemberEnd() ||
      !metadata->value.IsObject() || !payload->value.IsObject()) {
    return fit::error("Expected metadata and payload objects");
  }

  auto timestamp = metadata->value.FindMember("timestamp");
  if (timestamp == metadata->value.MemberEnd() || !timestamp->value.IsUint64()) {
    return fit::error("Expected metadata.timestamp key");
  }
  ret.time = timestamp->value.GetUint64();

  auto severity = metadata->value.FindMember("severity");
  if (severity == metadata->value.MemberEnd() || !severity->value.IsString()) {
    return fit::error("Expected metadata.severity key");
  }
  ret.severity = StringToSeverity(severity->value.GetString());

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
      return fit::error("Expected payload.root to be an object if present");
    }
  }

  for (auto it = payload->value.MemberBegin(); it != payload->value.MemberEnd(); ++it) {
    if (!it->name.IsString()) {
      return fit::error("A key is not a string");
    }
    std::string name = it->name.GetString();
    if (name == kMessageLabel && it->value.IsString()) {
      ret.msg = std::move(it->value.GetString());
    } else if (name == kTagLabel) {
      // TODO(fxbug.dev/63007): Parse only "tags"
      if (!it->value.IsString()) {
        return fit::error("Tag field must contain a single string value");
      }
      ret.tags.emplace_back(std::move(it->value.GetString()));
    } else if (name == kTagsLabel) {
      if (it->value.IsString()) {
        ret.tags.emplace_back(std::move(it->value.GetString()));
      } else if (it->value.IsArray()) {
        for (rapidjson::SizeType i = 0; i < it->value.Size(); ++i) {
          auto& val = it->value[i];
          if (!val.IsString()) {
            return fit::error("Tags array must contain strings");
          }
          ret.tags.emplace_back(std::move(val.GetString()));
        }
      } else {
        return fit::error("Tags must be a string or array of strings");
      }
    } else if (name == kTidLabel && it->value.IsUint64()) {
      ret.tid = it->value.GetUint64();
    } else if (name == kPidLabel && it->value.IsUint64()) {
      ret.pid = it->value.GetUint64();
    } else {
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

  ret.msg += kv_mapping.str();

  // If there are no tags, automatically tag with the component moniker.
  if (ret.tags.size() == 0 && !moniker_string.empty()) {
    ret.tags.emplace_back(std::move(moniker_string));
  }

  if (dropped_logs > 0) {
    ret.dropped_logs = dropped_logs;
  }

  return fit::ok(std::move(ret));
}
}  // namespace

fit::result<std::vector<fit::result<fuchsia::logger::LogMessage, std::string>>, std::string>
ConvertFormattedContentToLogMessages(FormattedContent content) {
  std::vector<fit::result<LogMessage, std::string>> output;

  if (!content.is_json()) {
    // Expecting JSON in all cases.
    return fit::error("Expected json content");
  }

  std::string data;
  if (!fsl::StringFromVmo(content.json(), &data)) {
    return fit::error("Failed to read string from VMO");
  }
  content.json().vmo.reset();

  rapidjson::Document d;
  d.Parse(std::move(data));
  if (d.HasParseError()) {
    std::string error = "Failed to parse content as JSON. Offset " +
                        std::to_string(d.GetErrorOffset()) + ": " +
                        rapidjson::GetParseError_En(d.GetParseError());
    return fit::error(std::move(error));
  }

  if (!d.IsArray()) {
    return fit::error("Expected content to contain an array");
  }

  for (rapidjson::SizeType i = 0; i < d.Size(); ++i) {
    output.emplace_back(JsonToLogMessage(d[i]));
  }

  return fit::ok(std::move(output));
}

}  // namespace diagnostics::stream
