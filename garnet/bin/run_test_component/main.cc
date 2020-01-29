// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/debugdata/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/process/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/termination_reason.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <stdio.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <memory>
#include <string>

#include "garnet/bin/run_test_component/env_config.h"
#include "garnet/bin/run_test_component/run_test_component.h"
#include "garnet/bin/run_test_component/test_metadata.h"
#include "lib/vfs/cpp/service.h"
#include "src/lib/files/file.h"
#include "src/lib/files/glob.h"
#include "src/lib/fxl/strings/string_printf.h"

using fuchsia::sys::TerminationReason;

namespace {
constexpr char kEnvPrefix[] = "test_env_";
constexpr char kConfigPath[] = "/pkgfs/packages/run_test_component/0/data/environment.config";

void PrintUsage() {
  fprintf(stderr, R"(
Usage: run_test_component [--realm-label=<label>] <test_url>|<test_matcher> [arguments...]

       *test_url* takes the form of component manifest URL which uniquely
       identifies a test component. Example:
          fuchsia-pkg://fuchsia.com/component_hello_world#meta/hello.cmx

       if *test_matcher* is provided, this tool will use component index
       to find matching component. If multiple urls are found, it will
       print corresponding component URLs and exit.  If there is only
       one match, it will generate a component URL and execute the test.

       example:
        run_test_component run_test_component_unit
          will match fuchsia-pkg://fuchsia.com/run_test_component_unittests#meta/run_test_component_unittests.cmx and run it.

       By default each test component will be run in an environment with
       transient storage and a randomly-generated identifier, ensuring that
       the tests have no persisted side-effects. If --realm-label is
       specified then the test will run in a persisted realm with that label,
       allowing files to be provide to, or retrieve from, the test, e.g. for
       diagnostic purposes.
)");
}

bool ConnectToRequiredEnvironment(const run::EnvironmentType& env_type, zx::channel request) {
  std::string current_env;
  files::ReadFileToString("/hub/name", &current_env);
  std::string svc_path = "/hub/svc";
  switch (env_type) {
    case run::EnvironmentType::SYS:
      if (current_env == "app") {
        files::Glob glob("/hub/r/sys/*/svc");
        if (glob.size() != 1) {
          fprintf(stderr, "Cannot run test. Something wrong with hub.");
          return false;
        }
        svc_path = *(glob.begin());
      } else if (current_env != "sys") {
        fprintf(stderr,
                "Cannot run test in sys environment as this utility was "
                "started in '%s' environment",
                current_env.c_str());
        return false;
      }
      break;
  }

  // launch test
  zx::channel h1, h2;
  zx_status_t status;
  if ((status = zx::channel::create(0, &h1, &h2)) != ZX_OK) {
    fprintf(stderr, "Cannot create channel, status: %d", status);
    return false;
  }
  if ((status = fdio_service_connect(svc_path.c_str(), h1.release())) != ZX_OK) {
    fprintf(stderr, "Cannot connect to %s, status: %d", svc_path.c_str(), status);
    return false;
  }

  if ((status = fdio_service_connect_at(h2.get(), fuchsia::sys::Environment::Name_,
                                        request.release())) != ZX_OK) {
    fprintf(stderr, "Cannot connect to env service, status: %d", status);
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, const char** argv) {
  run::EnvironmentConfig config;
  config.ParseFromFile(kConfigPath);
  if (config.HasError()) {
    fprintf(stderr, "Error parsing config file %s: %s\n", kConfigPath, config.error_str().c_str());
    return 1;
  }

  // Services which we get from /svc. They might be different depending on in which shell this
  // binary is launched from, so can't use it to create underlying environment.
  auto namespace_services = sys::ServiceDirectory::CreateFromNamespace();
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto parse_result = run::ParseArgs(namespace_services, argc, argv);
  if (parse_result.error) {
    if (parse_result.error_msg != "") {
      fprintf(stderr, "%s\n", parse_result.error_msg.c_str());
    }
    PrintUsage();
    return 1;
  }
  if (parse_result.matching_urls.size() > 1) {
    fprintf(stderr, "Found multiple matching components. Did you mean?\n");
    for (auto url : parse_result.matching_urls) {
      fprintf(stderr, "%s\n", url.c_str());
    }
    return 1;
  } else if (parse_result.matching_urls.size() == 1) {
    fprintf(stdout, "Found one matching component. Running: %s\n",
            parse_result.matching_urls[0].c_str());
  }
  std::string program_name = parse_result.launch_info.url;

  // We make a request to the resolver API to ensure that the on-disk package
  // data is up to date before continuing to try and parse the CMX file.
  // TODO(raggi): replace this with fuchsia.pkg.Resolver, once it is stable.
  fuchsia::process::ResolverSyncPtr resolver;
  zx_status_t status =
      namespace_services->Connect<fuchsia::process::Resolver>(resolver.NewRequest());
  if (status != ZX_OK) {
    fprintf(stderr, "connect to %s failed: %s. Can not continue.\n",
            fuchsia::process::Resolver::Name_, zx_status_get_string(status));
    return 1;
  }
  zx::vmo cmx_data;
  fidl::InterfaceHandle<::fuchsia::ldsvc::Loader> ldsvc_unused;
  resolver->Resolve(program_name, &status, &cmx_data, &ldsvc_unused);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to resolve %s: %s\n", program_name.c_str(),
            zx_status_get_string(status));
    return 1;
  }

  uint64_t size;
  status = cmx_data.get_size(&size);
  if (status != ZX_OK) {
    fprintf(stderr, "error getting size of cmx file from vmo %s: %s\n", program_name.c_str(),
            zx_status_get_string(status));
    return 1;
  }
  std::string cmx_str(size, ' ');
  status = cmx_data.read(cmx_str.data(), 0, size);
  if (status != ZX_OK) {
    fprintf(stderr, "error reading cmx file from vmo %s: %s\n", program_name.c_str(),
            zx_status_get_string(status));
    return 1;
  }

  run::TestMetadata test_metadata;
  if (!test_metadata.ParseFromString(cmx_str, program_name)) {
    fprintf(stderr, "Error parsing cmx %s: %s\n", program_name.c_str(),
            test_metadata.error_str().c_str());
    return 1;
  }

  fuchsia::sys::ComponentControllerPtr controller;
  fuchsia::sys::EnvironmentPtr parent_env;
  fuchsia::sys::LauncherPtr launcher;
  std::unique_ptr<sys::testing::EnclosingEnvironment> enclosing_env;

  auto map_entry = config.url_map().find(parse_result.launch_info.url);
  if (map_entry != config.url_map().end()) {
    if (test_metadata.HasServices()) {
      fprintf(stderr,
              "Cannot run this test in sys/root environment as it defines "
              "services in its '%s' facets\n",
              run::kFuchsiaTest);
      return 1;
    }
    if (!ConnectToRequiredEnvironment(map_entry->second, parent_env.NewRequest().TakeChannel())) {
      return 1;
    }
    parse_result.launch_info.out = sys::CloneFileDescriptor(STDOUT_FILENO);
    parse_result.launch_info.err = sys::CloneFileDescriptor(STDERR_FILENO);
    parent_env->GetLauncher(launcher.NewRequest());
  } else {
    namespace_services->Connect(parent_env.NewRequest());

    // Our bots run tests in zircon shell which do not have all required services, so create the
    // test environment from `parent_env` (i.e. the sys environment) instead of the services in the
    // namespace. But pass DebugData from the namespace because it is not available in `parent_env`.
    sys::testing::EnvironmentServices::ParentOverrides parent_overrides;
    parent_overrides.debug_data_service_ =
        std::make_shared<vfs::Service>([namespace_services = namespace_services](
                                           zx::channel channel, async_dispatcher_t* /*unused*/) {
          namespace_services->Connect(fuchsia::debugdata::DebugData::Name_, std::move(channel));
        });

    auto test_env_services = sys::testing::EnvironmentServices::CreateWithParentOverrides(
        parent_env, std::move(parent_overrides));
    auto services = test_metadata.TakeServices();
    bool provide_real_log_sink = true;
    for (auto& service : services) {
      test_env_services->AddServiceWithLaunchInfo(std::move(service.second), service.first);
      if (service.first == fuchsia::logger::LogSink::Name_) {
        // don't add global log sink service if test component is injecting
        // it.
        provide_real_log_sink = false;
      }
    }
    if (provide_real_log_sink) {
      test_env_services->AllowParentService(fuchsia::logger::LogSink::Name_);
    }
    auto& system_services = test_metadata.system_services();
    for (auto& service : system_services) {
      test_env_services->AllowParentService(service);
    }

    // By default run tests in a realm with a random name and transient storage.
    // Callers may specify a static realm label through which to exchange files
    // with the test component.
    std::string env_label = std::move(parse_result.realm_label);
    fuchsia::sys::EnvironmentOptions env_opt;
    if (env_label.empty()) {
      uint32_t rand;
      zx_cprng_draw(&rand, sizeof(rand));
      env_label = fxl::StringPrintf("%s%08x", kEnvPrefix, rand);
      env_opt.delete_storage_on_death = true;
    }

    enclosing_env = sys::testing::EnclosingEnvironment::Create(
        std::move(env_label), parent_env, std::move(test_env_services), std::move(env_opt));
    launcher = enclosing_env->launcher_ptr();
    printf("Running test in realm: %s\n", env_label.c_str());
  }

  launcher->CreateComponent(std::move(parse_result.launch_info), controller.NewRequest());

  int64_t ret_code = 1;

  controller.events().OnTerminated =
      [&ret_code, &program_name, &loop](int64_t return_code, TerminationReason termination_reason) {
        if (termination_reason != TerminationReason::EXITED) {
          fprintf(stderr, "%s: %s\n", program_name.c_str(),
                  sys::HumanReadableTerminationReason(termination_reason).c_str());
        }

        ret_code = return_code;

        loop.Quit();
      };

  loop.Run();

  // Wait and process all messages in the queue.
  loop.ResetQuit();
  loop.RunUntilIdle();

  return ret_code;
}
