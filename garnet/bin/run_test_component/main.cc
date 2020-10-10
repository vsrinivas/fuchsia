// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/debugdata/cpp/fidl.h>
#include <fuchsia/diagnostics/test/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/termination_reason.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/service.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "garnet/bin/run_test_component/component.h"
#include "garnet/bin/run_test_component/log_collector.h"
#include "garnet/bin/run_test_component/max_severity_config.h"
#include "garnet/bin/run_test_component/run_test_component.h"
#include "garnet/bin/run_test_component/sys_tests.h"
#include "garnet/bin/run_test_component/test_metadata.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/zx/object_traits.h"
#include "src/lib/files/file.h"
#include "src/lib/files/glob.h"
#include "src/lib/fxl/strings/string_printf.h"

using fuchsia::sys::TerminationReason;

namespace {
constexpr char kEnvPrefix[] = "test_env_";
const uint64_t kMillisInSec = 1000UL;
const uint64_t kMicrosInSec = 1000000UL;
const uint64_t kNanosInSec = 1000000000UL;

const std::string max_severity_config_path =
    "/pkgfs/packages/config-data/0/meta/data/run_test_component";

void PrintUsage() {
  fprintf(stderr, R"(
Usage: run_test_component [--realm-label=<label>] [--timeout=<seconds>] [--min-severity-logs=string] [--max-log-severity=string] <test_url>|<test_matcher> [arguments...]

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

       If --timeout is specified, test would be killed in <timeout> secs and
       run_test_component will exit with -ZX_ERR_TIMED_OUT.

       If --max-log-severity is passed, then the test will fail if it produces logs with higher severity.
       Allowed values: TRACE, DEBUG, INFO, WARN, ERROR, FATAL.
       For more information see: https://fuchsia.dev/fuchsia-src/concepts/testing/test_component#restricting_log_severity

       By default when installing log listener, all logs are collected. To filter
       by higher severity please pass severity: TRACE, DEBUG, INFO, WARN, ERROR, FATAL.
       example: run-test-component --min-severity-logs=WARN <url>
)");
}

bool ConnectToSysEnvironment(zx::channel request) {
  std::string current_env;
  files::ReadFileToString("/hub/name", &current_env);
  std::string svc_path = "/hub/svc";

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

std::string join_tags(std::vector<std::string> tags) {
  std::ostringstream stream;
  for (size_t i = 0; i < tags.size(); ++i) {
    if (i != 0) {
      stream << ",";
    }
    stream << tags[i];
  }
  return stream.str();
}

std::string log_level(int32_t severity) {
  switch (severity) {
    case syslog::LOG_TRACE:
      return "TRACE";
    case syslog::LOG_DEBUG:
      return "DEBUG";
    case syslog::LOG_INFO:
      return "INFO";
    case syslog::LOG_WARNING:
      return "WARNING";
    case syslog::LOG_ERROR:
      return "ERROR";
    case syslog::LOG_FATAL:
      return "FATAL";
  }
  if (severity > syslog::LOG_DEBUG && severity < syslog::LOG_INFO) {
    std::ostringstream stream;
    stream << "VLOG(" << -(syslog::LOG_INFO - severity) << ")";
    return stream.str();
  }
  return "INVALID";
}

std::unique_ptr<run::Component> launch_observer(const fuchsia::sys::LauncherPtr& launcher,
                                                async_dispatcher_t* dispatcher) {
  fuchsia::sys::LaunchInfo launch_info{
      .url = "fuchsia-pkg://fuchsia.com/archivist-for-embedding#meta/archivist-for-embedding.cmx"};
  launch_info.arguments = {"--disable-log-connector"};

  return run::Component::Launch(launcher, std::move(launch_info), dispatcher);
}

void print_log_message(const std::shared_ptr<fuchsia::logger::LogMessage>& log) {
  auto time = log->time;
  printf("[%05ld.%06ld][%ld][%ld][%s] %s: %s\n", time / kNanosInSec,
         (time / kMillisInSec) % kMicrosInSec, log->pid, log->tid, join_tags(log->tags).c_str(),
         log_level(log->severity).c_str(), log->msg.c_str());
}

void print_dropped_log_count(const std::shared_ptr<fuchsia::logger::LogMessage>& log) {
  auto time = log->time;
  printf("[%05ld.%06ld][%ld][%ld][%s] WARNING: Dropped logs count: %u\n", time / kNanosInSec,
         (time / kMillisInSec) % kMicrosInSec, log->pid, log->tid, join_tags(log->tags).c_str(),
         log->dropped_logs);
}

}  // namespace

int main(int argc, const char** argv) {
  auto max_severity_config = run::MaxSeverityConfig::ParseFromDirectory(max_severity_config_path);

  if (max_severity_config.HasError()) {
    fprintf(stderr,
            "WARN: max_severity config file(s) are broken: %s. Updatiing your device might fix "
            "the issue.",
            max_severity_config.Error().c_str());
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
  fuchsia::sys::LoaderSyncPtr loader;
  zx_status_t status = namespace_services->Connect<fuchsia::sys::Loader>(loader.NewRequest());
  if (status != ZX_OK) {
    fprintf(stderr, "connect to %s failed: %s. Can not continue.\n", fuchsia::sys::Loader::Name_,
            zx_status_get_string(status));
    return 1;
  }
  fuchsia::sys::PackagePtr pkg;
  status = loader->LoadUrl(program_name, &pkg);

  if (status != ZX_OK) {
    fprintf(stderr, "Failed to load %s: %s\n", program_name.c_str(), zx_status_get_string(status));
    return 1;
  }

  if (!pkg) {
    fprintf(stderr, "Got no package for %s\n", program_name.c_str());
    return 1;
  }

  if (!pkg->data) {
    fprintf(stderr, "Got no package metadata for %s\n", program_name.c_str());
    return 1;
  }

  uint64_t size = pkg->data->size;
  std::string cmx_str(size, ' ');
  status = pkg->data->vmo.read(cmx_str.data(), 0, size);
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

  fuchsia::sys::EnvironmentPtr parent_env;
  fuchsia::sys::LauncherPtr launcher;
  std::unique_ptr<sys::testing::EnclosingEnvironment> enclosing_env;

  std::vector<std::shared_ptr<fuchsia::logger::LogMessage>> restricted_logs;
  auto max_severity_allowed = parse_result.max_log_severity;
  bool restrict_logs = max_severity_allowed != syslog::LOG_FATAL;
  if (restrict_logs) {
    std::string simplified_url = run::GetSimplifiedUrl(program_name);
    auto it = max_severity_config.config().find(simplified_url);
    if (it != max_severity_config.config().end()) {
      // Default in BUILD.gn is WARNING. If the user overrides it give a warning that config is
      // preferred over BUILD.gn configuration.
      if (max_severity_allowed != syslog::LOG_WARNING) {
        printf(
            "\nWARNING: Test '%s' overrides max log severity in BUILD.gn as well as config file. "
            "Using the value from config file. If you want the test to pickup value from BUILD.gn, "
            "please remove the test url from the config file.\n See "
            "https://fuchsia.dev/fuchsia-src/concepts/testing/"
            "test_component#restricting_log_severity for more info.",
            program_name.c_str());
      }
      max_severity_allowed = it->second;
    }
  }

  auto log_collector = std::make_unique<run::LogCollector>(
      [dispather = loop.dispatcher(), restrict_logs, max_severity_allowed,
       &restricted_logs](fuchsia::logger::LogMessage log) {
        static std::map<uint32_t, uint32_t> dropped_logs_map;
        auto log_wrapper = std::make_shared<fuchsia::logger::LogMessage>(std::move(log));

        if (restrict_logs && log_wrapper->severity > max_severity_allowed) {
          restricted_logs.push_back(log_wrapper);
        }
        if (log_wrapper->dropped_logs > 0) {
          auto dropped_logs =
              dropped_logs_map[log_wrapper->pid];  // default initializer will set it
                                                   // to zero if key is not found.
          if (log_wrapper->dropped_logs > dropped_logs) {
            dropped_logs_map[log_wrapper->pid] = log_wrapper->dropped_logs;
            print_dropped_log_count(log_wrapper);
          }
        }
        async::PostTask(dispather, [log = std::move(log_wrapper)]() mutable {
          print_log_message(log);
          fflush(stdout);
        });
      });

  std::unique_ptr<run::Component> observer_component = nullptr;

  if (run::should_run_in_sys(parse_result.launch_info.url)) {
    if (test_metadata.HasServices()) {
      fprintf(stderr,
              "Cannot run this test in sys/root environment as it defines "
              "services in its '%s' facets\n",
              run::kFuchsiaTest);
      return 1;
    }
    if (!ConnectToSysEnvironment(parent_env.NewRequest().TakeChannel())) {
      return 1;
    }

    parent_env->GetLauncher(launcher.NewRequest());
  } else {
    namespace_services->Connect(parent_env.NewRequest());

    // Our bots run tests in zircon shell which do not have all required services, so create the
    // test environment from `parent_env` (i.e. the sys environment) instead of the services in
    // the namespace. But pass DebugData from the namespace because it is not available in
    // `parent_env`.
    sys::testing::EnvironmentServices::ParentOverrides parent_overrides;
    parent_overrides.debug_data_service_ =
        std::make_shared<vfs::Service>([namespace_services = namespace_services](
                                           zx::channel channel, async_dispatcher_t* /*unused*/) {
          namespace_services->Connect(fuchsia::debugdata::DebugData::Name_, std::move(channel));
        });

    auto test_env_services = sys::testing::EnvironmentServices::CreateWithParentOverrides(
        parent_env, std::move(parent_overrides));
    auto services = test_metadata.TakeServices();
    bool collect_isolated_logs = true;
    for (auto& service : services) {
      test_env_services->AddServiceWithLaunchInfo(std::move(service.second), service.first);
      if (service.first == fuchsia::logger::LogSink::Name_) {
        // don't add global log sink service if test component is injecting
        // it.
        collect_isolated_logs = false;
      }
    }
    if (collect_isolated_logs) {
      fuchsia::sys::LauncherPtr launcher;
      parent_env->GetLauncher(launcher.NewRequest());
      observer_component = launch_observer(launcher, loop.dispatcher());

      test_env_services->AddService<fuchsia::logger::LogSink>(
          [observer_svc = observer_component->svc()](
              fidl::InterfaceRequest<fuchsia::logger::LogSink> request) {
            observer_svc->Connect(std::move(request));
          });
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

    if (collect_isolated_logs) {
      ZX_ASSERT(observer_component != nullptr);
      // this will launch the service and also collect logs.
      auto log_ptr = observer_component->svc()->Connect<fuchsia::logger::Log>();

      fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe> log_listener;
      auto options = std::make_unique<fuchsia::logger::LogFilterOptions>();
      options->min_severity =
          static_cast<fuchsia::logger::LogLevelFilter>(parse_result.min_log_severity);

      log_collector->Bind(log_listener.NewRequest(), loop.dispatcher());
      log_ptr->ListenSafe(std::move(log_listener), std::move(options));
    }

    launcher = enclosing_env->launcher_ptr();
    printf("Running test in realm: %s\n", env_label.c_str());
  }

  auto test_component =
      run::Component::Launch(launcher, std::move(parse_result.launch_info), loop.dispatcher());

  int64_t ret_code = 1;

  bool timed_out = false;
  std::unique_ptr<async::TaskClosure> timeout_task;

  if (parse_result.timeout > 0) {
    timeout_task = std::make_unique<async::TaskClosure>(
        [&test_component, &program_name, &loop, &timed_out, &ret_code]() {
          test_component->controller()->Kill();
          timed_out = true;
          ret_code = -ZX_ERR_TIMED_OUT;
          fprintf(stderr, "%s canceled due to timeout.\n", program_name.c_str());
          loop.Quit();
        });

    timeout_task->PostDelayed(loop.dispatcher(), zx::sec(parse_result.timeout));
  }

  test_component->controller().events().OnTerminated =
      [&ret_code, &program_name, &loop, &timed_out](int64_t return_code,
                                                    TerminationReason termination_reason) {
        // component was killed due to timeout, don't collect results.
        if (timed_out) {
          return;
        }
        if (termination_reason != TerminationReason::EXITED) {
          fprintf(stderr, "%s: %s\n", program_name.c_str(),
                  sys::HumanReadableTerminationReason(termination_reason).c_str());
        }

        ret_code = return_code;

        loop.Quit();
      };

  loop.Run();
  loop.ResetQuit();

  // make sure timeout is not executed after test finishes.
  if (timeout_task && timeout_task->is_pending()) {
    timeout_task->Cancel();
  }

  // Wait and process all messages in the queue.
  loop.RunUntilIdle();

  if (observer_component) {
    ZX_ASSERT(enclosing_env);
    ZX_ASSERT(log_collector);
    enclosing_env->Kill([&loop]() { loop.Quit(); });

    loop.Run();
    loop.ResetQuit();

    // collect all logs
    log_collector->NotifyOnUnBind([&loop]() { loop.Quit(); });

    auto observer_ptr =
        observer_component->svc()->Connect<fuchsia::diagnostics::test::Controller>();
    observer_ptr->Stop();
    loop.Run();
    loop.ResetQuit();

    // now that observer is dead, make sure to collect its output
    loop.RunUntilIdle();
  }

  if (!restricted_logs.empty() && ret_code == 0) {
    printf("\nTest %s produced unexpected high-severity logs:\n", program_name.c_str());
    printf("----------------xxxxx----------------\n");
    for (const auto& log : restricted_logs) {
      print_log_message(log);
    }
    printf("----------------xxxxx----------------\n");
    printf(
        "Failing this test. See "
        "https://fuchsia.googlesource.com/fuchsia/+/master/docs/concepts/testing/"
        "test_component.md#restricting-log-severity for guidance.\n");
    fflush(stdout);
    ret_code = 1;
  }

  return ret_code;
}
