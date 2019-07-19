// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionctl/logger.h"

#include "peridot/bin/sessionctl/session_ctl_constants.h"

namespace modular {

const char kSuccessString[] = "success";
const char kCommandString[] = "command";

// Key strings for JSON output.
const char kParamsKeyString[] = "params";
const char kStoriesKeyString[] = "stories";

Logger::Logger(bool json_out) : json_out_(json_out) {}

void Logger::LogError(const std::string& command, const std::string& error) const {
  if (json_out_) {
    rapidjson::Document document;
    document.SetObject();

    document.AddMember(kSuccessString, false, document.GetAllocator());
    document.AddMember(kCommandString, command, document.GetAllocator());
    document.AddMember("error", error, document.GetAllocator());

    std::cout << JsonValueToPrettyString(document) << std::endl;
  } else {
    std::cout << error << std::endl;
  }
}

void Logger::Log(const std::string& command, const std::vector<std::string>& params) const {
  if (json_out_) {
    std::cout << GenerateJsonLogString(command, params) << std::endl;
  } else {
    std::stringstream output;

    if (command == kListStoriesCommandString) {
      output << "Stories in this session:" << std::endl;
    } else if (command == kDeleteAllStoriesCommandString) {
      output << "Deleted the following stories:" << std::endl;
    }

    for (auto& param : params) {
      output << param << std::endl;
    }

    std::cout << output.str() << std::endl;
  }
}

void Logger::Log(const std::string& command,
                 const std::map<std::string, std::string>& params) const {
  if (json_out_) {
    std::cout << GenerateJsonLogString(command, params) << std::endl;
  } else {
    std::cout << GenerateLogString(command, params) << std::endl;
  }
}

std::string Logger::GenerateJsonLogString(const std::string& command,
                                          const std::vector<std::string>& params) const {
  rapidjson::Document document = GetDocument(command);

  // Generate array of |params| strings.
  rapidjson::Document stories;
  stories.SetArray();
  for (auto& param : params) {
    rapidjson::Value story;
    story.SetString(param.data(), param.size());
    stories.PushBack(story, stories.GetAllocator());
  }

  // Determine what the strings in |params| represent.
  rapidjson::Value key;
  if (command == kListStoriesCommandString || command == kDeleteAllStoriesCommandString) {
    key.SetString(kStoriesKeyString);
  } else {
    key.SetString(kParamsKeyString);
  }

  document.AddMember(key, stories, document.GetAllocator());
  return JsonValueToPrettyString(document);
}

std::string Logger::GenerateJsonLogString(const std::string& command,
                                          const std::map<std::string, std::string>& params) const {
  rapidjson::Document document = GetDocument(command);

  // Generate a document containing |params| keys and values.
  rapidjson::Document paramsJson;
  paramsJson.SetObject();
  for (const auto& p : params) {
    rapidjson::Value name;
    name.SetString(p.first.data(), p.first.size());

    rapidjson::Value value;
    value.SetString(p.second.data(), p.second.size());
    paramsJson.AddMember(name, value, paramsJson.GetAllocator());
  }

  document.AddMember(kParamsKeyString, paramsJson, document.GetAllocator());
  return JsonValueToPrettyString(document);
}

rapidjson::Document Logger::GetDocument(const std::string& command) const {
  rapidjson::Document document;
  document.SetObject();
  document.AddMember(kSuccessString, true, document.GetAllocator());
  document.AddMember(kCommandString, command, document.GetAllocator());
  return document;
}

std::string Logger::GenerateLogString(const std::string& command,
                                      const std::map<std::string, std::string>& params) const {
  std::stringstream output;

  if (command == kDeleteStoryCommandString) {
    output << "Deleted";
  } else {
    if (command == kAddModCommandString) {
      output << "Added";
    } else if (command == kRemoveModCommandString) {
      output << "Removed";
    }

    output << " mod_name: " << params.at(kModNameFlagString).c_str() << " in";
  }

  output << " story_name: " << params.at(kStoryNameFlagString).c_str();
  return output.str();
}

}  // namespace modular
