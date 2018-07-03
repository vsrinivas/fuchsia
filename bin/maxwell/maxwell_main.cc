// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app/cpp/startup_context.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/strings/split_string.h>

#include "peridot/bin/maxwell/config.h"
#include "peridot/bin/maxwell/user_intelligence_provider_impl.h"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/schema.h"
#include "rapidjson/stringbuffer.h"

namespace maxwell {
namespace {

class App {
 public:
  App(fuchsia::sys::StartupContext* context, const Config& config)
      : factory_impl_(context, config) {
    context->outgoing()
        .AddPublicService<fuchsia::modular::UserIntelligenceProviderFactory>(
            [this](fidl::InterfaceRequest<
                   fuchsia::modular::UserIntelligenceProviderFactory>
                       request) {
              factory_bindings_.AddBinding(&factory_impl_, std::move(request));
            });
  }

 private:
  UserIntelligenceProviderFactoryImpl factory_impl_;
  fidl::BindingSet<fuchsia::modular::UserIntelligenceProviderFactory>
      factory_bindings_;
};

const char kConfigSchema[] = R"SCHEMA(
{
  "type": "object",
  "properties": {
    "startup_agents": {
      "type": "array",
      "items": { "type": "string" }
    },
    "kronk": { "type": "string" },
    "mi_dashboard": { "type": "boolean" }
  },
  "required": [ "startup_agents" ],
  "extra_properties": false
}
)SCHEMA";

bool LoadAndValidateConfig(const std::string& path, Config* out) {
  // Load the config datafile to a string.
  std::string data;
  if (!files::ReadFileToString(path, &data)) {
    FXL_LOG(WARNING) << "Missing config file: " << path;
    return false;
  }

  // Parse the JSON config.
  rapidjson::Document config_doc;
  config_doc.Parse(data);
  if (config_doc.HasParseError()) {
    fprintf(stderr, "Invalid config JSON (at %lu): %s\n",
            config_doc.GetErrorOffset(),
            rapidjson::GetParseError_En(config_doc.GetParseError()));
    return false;
  }

  // Initialize our schema.
  rapidjson::Document schema;
  FXL_DCHECK(!schema.Parse(kConfigSchema).HasParseError());
  rapidjson::SchemaDocument schema_doc(schema);

  // Validate the config against the schema.
  rapidjson::SchemaValidator validator(schema_doc);
  if (!config_doc.Accept(validator)) {
    rapidjson::StringBuffer uri_buffer;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(uri_buffer);
    std::cerr << "Startup config does not match schema at: "
              << uri_buffer.GetString()
              << " , schema violation: " << validator.GetInvalidSchemaKeyword();
    return false;
  }

  // Read values into the |out| struct.

  if (config_doc.HasMember("kronk")) {
    out->kronk = config_doc["kronk"].GetString();
  }

  if (config_doc.HasMember("mi_dashboard")) {
    out->mi_dashboard = config_doc["mi_dashboard"].GetBool();
  }

  for (const auto& agent : config_doc["startup_agents"].GetArray()) {
    out->startup_agents.push_back(agent.GetString());
  }

  return true;
}

}  // namespace
}  // namespace maxwell

const char kDefaultConfigPaths[] =
    "/pkg/data/maxwell/default_config.json,"
    "/pkg/data/maxwell/second_config.json";
const char kUsage[] = R"USAGE(%s --config=<files>

<files> = comma-separated list of paths to JSON configuration files
with the following format:

{
  "startup_agents": [
    "/path/to/binary1",
    "/path/to/binary2",
    ...
  ],
  "mi_dashboard": true/false,
}
)USAGE";

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (command_line.HasOption("help")) {
    printf(kUsage, argv[0]);
    return 0;
  }
  const std::string config_paths =
      command_line.GetOptionValueWithDefault("config", kDefaultConfigPaths);
  std::vector<std::string> config_paths_list = fxl::SplitStringCopy(
      config_paths, ",", fxl::kTrimWhitespace, fxl::kSplitWantAll);

  maxwell::Config config;
  if (config_paths_list.size() > 0) {
    // the first listed config file must exist and be valid
    if (!maxwell::LoadAndValidateConfig(config_paths_list[0], &config)) {
      FXL_LOG(FATAL) << "First config file missing or failed to load: "
                     << config_paths_list[0];
      return 1;
    }

    // Startup agents from all config files will be merged.  mi_dashboard and
    // other global settings will be superseded by later files.
    for (size_t i = 1; i < config_paths_list.size(); i++) {
      auto const& path = config_paths_list[i];
      if (files::IsFile(path) &&
          !maxwell::LoadAndValidateConfig(path, &config)) {
        FXL_LOG(WARNING) << "Config file failed to load: " << path;
      }
    }
  } else {
    FXL_LOG(FATAL) << "No config files specified.";
    return 1;
  }

  FXL_LOG(INFO) << "Starting Maxwell with config: \n" << config;

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto context = fuchsia::sys::StartupContext::CreateFromStartupInfo();
  maxwell::App app(context.get(), config);
  loop.Run();
  return 0;
}
