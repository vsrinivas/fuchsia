// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/process/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
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

#include "garnet/bin/run_test_component/env_config.h"
#include "garnet/bin/run_test_component/run_test_component.h"
#include "garnet/bin/run_test_component/test_metadata.h"
#include "src/lib/files/file.h"
#include "src/lib/files/glob.h"
#include "src/lib/fxl/strings/string_printf.h"

using fuchsia::sys::TerminationReason;

namespace {
constexpr char kEnvPrefix[] = "env_for_test_";
constexpr char kConfigPath[] =
    "/pkgfs/packages/run_test_component/0/data/environment.config";

void PrintUsage() {
  fprintf(stderr, R"(
Usage: run_test_component <test_url> [arguments...]
       run_test_component <test_matcher> [arguments...]

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
)");
}

bool ConnectToRequiredEnvironment(const run::EnvironmentType& env_type,
                                  zx::channel request) {
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
  if ((status = fdio_service_connect(svc_path.c_str(), h1.release())) !=
      ZX_OK) {
    fprintf(stderr, "Cannot connect to %s, status: %d", svc_path.c_str(),
            status);
    return false;
  }

  if ((status = fdio_service_connect_at(
           h2.get(), fuchsia::sys::Environment::Name_, request.release())) !=
      ZX_OK) {
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
    fprintf(stderr, "Error parsing config file %s: %s\n", kConfigPath,
            config.error_str().c_str());
    return 1;
  }
  auto env_services = sys::ServiceDirectory::CreateFromNamespace();
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto parse_result = run::ParseArgs(env_services, argc, argv);
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
  // TODO(anmittal): Ideally we could use the VMO returned by the resolver to
  // parse the data, but there are many layers of indirection between us and the
  // JSON parser that do not know how to talk anything but file paths, so that
  // was not yet done.
  {
    // TODO(raggi): replace this with fuchsia.pkg.Resolver, once it is stable.
    fuchsia::process::ResolverSyncPtr resolver;
    auto svc_dir = sys::ServiceDirectory::CreateFromNamespace();
    zx_status_t status =
        svc_dir->Connect<fuchsia::process::Resolver>(resolver.NewRequest());
    if (status != ZX_OK) {
      fprintf(stderr, "connect to %s failed: %s. Can not continue.\n",
              fuchsia::process::Resolver::Name_, zx_status_get_string(status));
      return 1;
    }
    zx::vmo cmx_unused;
    fidl::InterfaceHandle<::fuchsia::ldsvc::Loader> ldsvc_unused;
    resolver->Resolve(program_name, &status, &cmx_unused, &ldsvc_unused);
    if (status != ZX_OK) {
      fprintf(stderr, "Failed to resolve %s: %s\n", program_name.c_str(),
              zx_status_get_string(status));
      return 1;
    }
  }

  run::TestMetadata test_metadata;
  if (!test_metadata.ParseFromFile(parse_result.cmx_file_path)) {
    fprintf(stderr, "Error parsing cmx %s: %s\n",
            parse_result.cmx_file_path.c_str(),
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
    if (!ConnectToRequiredEnvironment(map_entry->second,
                                      parent_env.NewRequest().TakeChannel())) {
      return 1;
    }
    parse_result.launch_info.out = sys::CloneFileDescriptor(STDOUT_FILENO);
    parse_result.launch_info.err = sys::CloneFileDescriptor(STDERR_FILENO);
    parent_env->GetLauncher(launcher.NewRequest());
  } else {
    env_services->Connect(parent_env.NewRequest());
    auto env_services = sys::testing::EnvironmentServices::Create(parent_env);
    auto services = test_metadata.TakeServices();
    bool provide_real_log_sink = true;
    for (auto& service : services) {
      env_services->AddServiceWithLaunchInfo(std::move(service.second),
                                             service.first);
      if (service.first == fuchsia::logger::LogSink::Name_) {
        // don't add global log sink service if test component is injecting
        // it.
        provide_real_log_sink = false;
      }
    }
    if (provide_real_log_sink) {
      env_services->AllowParentService(fuchsia::logger::LogSink::Name_);
    }
    auto& system_services = test_metadata.system_services();
    for (auto& service : system_services) {
      env_services->AllowParentService(service);
    }

    uint32_t rand;
    zx_cprng_draw(&rand, sizeof(rand));
    std::string env_label = fxl::StringPrintf("%s%08x", kEnvPrefix, rand);
    fuchsia::sys::EnvironmentOptions env_opt{.delete_storage_on_death = true};
    enclosing_env = sys::testing::EnclosingEnvironment::Create(
        std::move(env_label), parent_env, std::move(env_services),
        std::move(env_opt));
    launcher = enclosing_env->launcher_ptr();
  }

  launcher->CreateComponent(std::move(parse_result.launch_info),
                            controller.NewRequest());

  controller.events().OnTerminated = [&program_name](
                                         int64_t return_code,
                                         TerminationReason termination_reason) {
    if (termination_reason != TerminationReason::EXITED) {
      fprintf(stderr, "%s: %s\n", program_name.c_str(),
              sys::HumanReadableTerminationReason(termination_reason).c_str());
    }
    zx_process_exit(return_code);
  };

  loop.Run();
  fprintf(stderr, "run-test-component: premature loop exit\n");
  return 1;
}
