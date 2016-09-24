// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A bootstrapping application that parses command line arguments and starts up
// the TQ runtime flow. Configuration information for applications is managed in
// //apps/modular/fuchsia-scripts/boot_config.json.
// The configuration file should be specified like so:
// {
//   "args-for": {
//      "mojo:dummy_device_shell": ["user1"]
//   }
// }

#include "lib/ftl/files/file.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"
#include "mojo/application_manager/application_manager.h"

#include <rapidjson/document.h>

namespace {

constexpr char kBootConfigPath[] = "/boot/data/modular/boot_config.json";

constexpr char kArgsFor[] = "args-for";

bool ParseArgsFor(const rapidjson::Value& value,
                  std::unordered_map<std::string, std::vector<std::string>>*
                      output_args_for) {
  FTL_DCHECK(output_args_for);
  FTL_DCHECK(value.IsObject());

  std::unordered_map<std::string, std::vector<std::string>> args_for;
  for (const auto& command : value.GetObject()) {
    if (!command.name.IsString()) return false;
    std::string application_name(command.name.GetString());

    if (!command.value.IsArray()) return false;
    std::vector<std::string> args;
    for (const auto& arg : command.value.GetArray()) {
      if (!arg.IsString()) return false;
      args.push_back(std::string(arg.GetString()));
    }
    args_for.emplace(std::move(application_name), std::move(args));
  }

  output_args_for->swap(args_for);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string result;
  if (!files::ReadFileToString(kBootConfigPath, &result)) {
    FTL_LOG(INFO) << "Failed to read config file, bailing out.";
    return 1;
  }

  rapidjson::Document document;
  document.Parse(result.data(), result.size());
  if (!document.IsObject()) {
    FTL_LOG(INFO) << "Config was not an object, bailing out.";
    return 1;
  }

  rapidjson::Value::ConstMemberIterator args_for_itr =
      document.FindMember(kArgsFor);
  std::unordered_map<std::string, std::vector<std::string>> args_for;
  if (args_for_itr != document.MemberEnd()) {
    if (!ParseArgsFor(args_for_itr->value, &args_for)) {
      FTL_LOG(INFO) << "Failed to parse args-for, bailing out.";
      return 1;
    }
  }

  mojo::ApplicationManager manager(std::move(args_for));
  mtl::MessageLoop message_loop;
  message_loop.task_runner()->PostTask([&manager]() {
    if (!manager.StartInitialApplication("mojo:device_runner")) {
      exit(1);
    }
  });

  message_loop.Run();
  return 0;
}
