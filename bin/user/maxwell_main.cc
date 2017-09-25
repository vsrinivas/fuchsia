// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/file.h"
#include "lib/user_intelligence/fidl/user_intelligence_provider.fidl.h"
#include "peridot/bin/user/config.h"
#include "peridot/bin/user/user_intelligence_provider_impl.h"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/schema.h"
#include "rapidjson/stringbuffer.h"

namespace maxwell {
namespace {

class App {
 public:
  App(app::ApplicationContext* app_context, const Config& config)
      : factory_impl_(app_context, config) {
    auto services = app_context->outgoing_services();
    services->AddService<UserIntelligenceProviderFactory>(
        [this](
            fidl::InterfaceRequest<UserIntelligenceProviderFactory> request) {
          factory_bindings_.AddBinding(&factory_impl_, std::move(request));
        });
  }

 private:
  UserIntelligenceProviderFactoryImpl factory_impl_;
  fidl::BindingSet<UserIntelligenceProviderFactory> factory_bindings_;
};

const char kConfigSchema[] = R"SCHEMA(
{
  "type": "object",
  "properties": {
    "startup_agents": {
      "type": "array",
      "items": { "type": "string" }
    },
    "mi_dashboard": { "type": "boolean" }
  },
  "required": [ "startup_agents", "mi_dashboard" ],
  "extra_properties": false
}
)SCHEMA";

bool LoadAndValidateConfig(const std::string& path, Config* out) {
  // Load the config datafile to a string.
  std::string data;
  if (!files::ReadFileToString(path, &data)) {
    FXL_LOG(FATAL) << "Missing config file: " << path;
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
  out->mi_dashboard = config_doc["mi_dashboard"].GetBool();
#ifdef DEPRECATED_NO_MI_DASHBOARD
  // TODO(thatguy): Remove this once references to it in Modular tests
  // have been removed.
  out->mi_dashboard = false;
#endif

  for (const auto& agent : config_doc["startup_agents"].GetArray()) {
    out->startup_agents.push_back(agent.GetString());
  }

  return true;
}

}  // namespace
}  // namespace maxwell

const char kDefaultConfigPath[] = "/system/data/maxwell/default_config.json";
const char kUsage[] = R"USAGE(%s --config=<file>

<file> = path to a JSON configuration file with the following format:

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
  const std::string config_path =
      command_line.GetOptionValueWithDefault("config", kDefaultConfigPath);

  maxwell::Config config;
  if (!maxwell::LoadAndValidateConfig(config_path, &config)) {
    return 1;
  }

  FXL_LOG(INFO) << "Starting Maxwell with config: \n" << config;

  fsl::MessageLoop loop;
  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  maxwell::App app(app_context.get(), config);
  loop.Run();
  return 0;
}