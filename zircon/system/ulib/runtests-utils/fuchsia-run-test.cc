// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/debugdata/datasink.h>
#include <lib/debugdata/debugdata.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fit/defer.h>
#include <lib/zx/clock.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <forward_list>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <runtests-utils/fuchsia-run-test.h>
#include <runtests-utils/service-proxy-dir.h>

#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace runtests {

namespace {

// Path to helper binary which can run test as a component. This binary takes
// component url as its parameter.
constexpr char kRunTestComponentPath[] = "/bin/run-test-component";

// Path to helper binary which can run test as a v2 component. This binary takes
// component url as its parameter.
constexpr char kRunTestSuitePath[] = "/bin/run-test-suite";

fbl::String RootName(const fbl::String& path) {
  const size_t i = strspn(path.c_str(), "/");
  const char* start = &path.c_str()[i];
  const char* end = strchr(start, '/');
  if (end == nullptr) {
    end = &path.c_str()[path.size()];
  }
  return fbl::String::Concat({"/", fbl::String(start, end - start)});
}

}  // namespace

bool SetUpForTestComponent(const char* test_path, fbl::String* out_component_executor) {
  if (IsFuchsiaPkgURI(test_path)) {
    const char* last_three_chars_of_url = &(test_path[strlen(test_path) - 3]);
    if (0 == strncmp(last_three_chars_of_url, "cmx", 3)) {  // v1 component
      *out_component_executor = kRunTestComponentPath;
    } else if (0 == strncmp(last_three_chars_of_url, ".cm", 3)) {  // v2
      *out_component_executor = kRunTestSuitePath;
    } else {
      fprintf(stderr, "FAILURE: component URL has unexpected format: %s\n", test_path);
      return false;
    }
  } else if (0 == strncmp(test_path, kPkgPrefix, strlen(kPkgPrefix))) {
    fprintf(stderr, "FAILURE: Test path '%s' starts with %s, which is not supported.\n", test_path,
            kPkgPrefix);
    return false;
  }

  return true;
}

// Run the loop until |deadline|, or until the specified |signal| is asserted on the |handle|.
zx_status_t RunLoopUntilSignalOrDeadline(async::Loop& loop, zx::time deadline, zx_handle_t handle,
                                         zx_signals_t signal) {
  async::WaitOnce process_wait(handle, signal);
  zx_status_t wait_status = ZX_ERR_TIMED_OUT;
  process_wait.Begin(loop.dispatcher(), [&loop, &wait_status](async_dispatcher_t*, async::WaitOnce*,
                                                              zx_status_t wait_status_inner,
                                                              const zx_packet_signal_t*) {
    wait_status = wait_status_inner;
    loop.Quit();
  });
  loop.Run(deadline);
  loop.ResetQuit();
  return wait_status;
}

std::unique_ptr<Result> RunTest(const char* argv[], const char* output_dir, const char* test_name,
                                int64_t timeout_msec, const char* realm_label) {
  // The arguments passed to fdio_spawn_etc. May be overridden.
  const char** args = argv;
  // calculate size of argv
  size_t argc = 0;
  while (argv[argc] != nullptr) {
    argc++;
  }

  const char* path = argv[0];
  fbl::String component_executor;
  fbl::String realm_label_arg;

  if (!SetUpForTestComponent(path, &component_executor)) {
    return std::make_unique<Result>(path, FAILED_TO_LAUNCH, 0, 0);
  }

  const char* component_launch_args[argc + 4];
  if (realm_label != nullptr) {
    realm_label_arg = fbl::String::Concat({"--realm-label=", realm_label});
  }
  if (component_executor.length() > 0) {
    // Check whether the executor is present and print a more helpful error, rather than failing
    // later in the fdio_spawn_etc call.
    struct stat s;
    if (stat(component_executor.c_str(), &s)) {
      fprintf(stderr,
              "FAILURE: Cannot find '%s', cannot run %s as component."
              "binary.\n",
              component_executor.c_str(), path);
      return std::make_unique<Result>(path, FAILED_TO_LAUNCH, 0, 0);
    }
    component_launch_args[0] = component_executor.c_str();
    int j = 1;
    if (realm_label != nullptr) {
      component_launch_args[j] = realm_label_arg.c_str();
      j++;
    }
    component_launch_args[j] = path;
    j++;
    for (size_t i = 1; i <= argc; i++) {
      component_launch_args[j] = argv[i];
      j++;
    }
    args = component_launch_args;
  }

  // Truncate the name on the left so the more important stuff on the right part of the path stays
  // in the name.
  const char* test_name_trunc = test_name;
  size_t test_name_length = strlen(test_name_trunc);
  if (test_name_length > ZX_MAX_NAME_LEN - 1) {
    test_name_trunc += test_name_length - (ZX_MAX_NAME_LEN - 1);
  }

  fbl::Vector<fdio_spawn_action_t> fdio_actions = {
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_SET_NAME,
          .name =
              {
                  .data = test_name_trunc,
              },
      },
  };

  std::optional<fs::SynchronousVfs> vfs;
  // This must be declared after the vfs so that its destructor gets called before the vfs
  // destructor. We do this explicitly at the end of the function in the non-error case, but in
  // error cases we just rely on the destructors to clean things up.
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  bool data_collection_err_occurred = false;
  fbl::unique_fd data_sink_dir_fd;
  std::optional<debugdata::Publisher> debug_data_publisher;
  std::optional<debugdata::DataSink> debug_data_sink;
  debugdata::DataSinkCallback on_data_collection_error_callback = [&](const std::string& error) {
    fprintf(stderr, "FAILURE: %s\n", error.c_str());
    data_collection_err_occurred = true;
  };
  debugdata::DataSinkCallback on_data_collection_warning_callback =
      [&](const std::string& warning) { fprintf(stderr, "WARNING: %s\n", warning.c_str()); };

  auto action_ns_entry = [](const char* prefix, zx_handle_t handle) {
    return fdio_spawn_action{
        .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
        .ns =
            {
                .prefix = prefix,
                .handle = handle,
            },
    };
  };

  // If |output_dir| is provided, set up the loader and debugdata services that will be
  // used to capture any data published.
  uint32_t fdio_flags = FDIO_SPAWN_CLONE_ALL;
  if (output_dir != nullptr) {
    fdio_flags &= ~FDIO_SPAWN_CLONE_NAMESPACE;

    fbl::unique_fd root_dir_fd{open("/", O_RDONLY | O_DIRECTORY)};
    if (!root_dir_fd) {
      fprintf(stderr, "FAILURE: Could not open root directory /\n");
      return std::make_unique<Result>(path, FAILED_UNKNOWN, 0, 0);
    }

    data_sink_dir_fd = fbl::unique_fd(open(output_dir, O_RDONLY | O_DIRECTORY));
    if (!data_sink_dir_fd) {
      fprintf(stderr, "FAILURE: Could not open output directory %s: %s\n", output_dir,
              strerror(errno));
      return std::make_unique<Result>(path, FAILED_UNKNOWN, 0, 0);
    }

    // Setup DebugData service implementation.
    debug_data_sink.emplace(data_sink_dir_fd);
    debug_data_publisher.emplace(
        loop.dispatcher(), std::move(root_dir_fd), [&](std::string data_sink, zx::vmo vmo) {
          debug_data_sink->ProcessSingleDebugData(data_sink, std::move(vmo), {},
                                                  on_data_collection_error_callback,
                                                  on_data_collection_warning_callback);
        });

    auto node = fbl::MakeRefCounted<fs::Service>(
        [dispatcher = loop.dispatcher(),
         &debug_data_publisher](fidl::ServerEnd<fuchsia_debugdata::Publisher> channel) {
          debug_data_publisher->Bind(std::move(channel), dispatcher);
          return ZX_OK;
        });

    vfs.emplace(loop.dispatcher());

    std::tuple<const char*, int> map[] = {
        {"/boot", O_RDONLY},
        {"/svc", O_RDONLY},
        {"/tmp", O_RDWR},
    };
    for (auto [path, flags] : map) {
      fbl::unique_fd fd{open(path, flags | O_DIRECTORY)};
      if (!fd) {
        fprintf(stderr, "FAILURE: Could not open directory %s: %s\n", path, strerror(errno));
        return std::make_unique<Result>(path, FAILED_UNKNOWN, 0, 0);
      }
      fdio_cpp::FdioCaller caller(std::move(fd));
      zx::result result = caller.take_directory();
      if (result.is_error()) {
        fprintf(stderr, "FAILURE: Could not take directory %s channel: %s\n", path,
                result.status_string());
        return std::make_unique<Result>(path, FAILED_UNKNOWN, 0, 0);
      }
      fidl::ClientEnd client_end = std::move(result).value();

      if (strcmp(path, "/svc") == 0) {
        zx::result endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
        if (endpoints.is_error()) {
          fprintf(stderr, "FAILURE: Could not create endpoints: %s\n", endpoints.status_string());
          return std::make_unique<Result>(path, FAILED_UNKNOWN, 0, 0);
        }

        fbl::RefPtr proxy_dir = fbl::MakeRefCounted<ServiceProxyDir>(std::move(client_end));
        proxy_dir->AddEntry(fidl::DiscoverableProtocolName<fuchsia_debugdata::Publisher>, node);

        vfs->ServeDirectory(std::move(proxy_dir), std::move(endpoints->server), fs::Rights::All());

        fdio_actions.push_back(action_ns_entry(path, endpoints->client.channel().release()));
      } else {
        fdio_actions.push_back(action_ns_entry(path, client_end.TakeChannel().release()));
      }
    }
  }

  zx_status_t status;

  zx::job test_job;
  status = zx::job::create(*zx::job::default_job(), 0, &test_job);
  if (status != ZX_OK) {
    fprintf(stderr, "FAILURE: zx::job::create() returned %d\n", status);
    return std::make_unique<Result>(test_name, FAILED_TO_LAUNCH, 0, 0);
  }
  auto auto_call_kill_job = fit::defer([&test_job]() { test_job.kill(); });
  status = test_job.set_property(ZX_PROP_NAME, "run-test", sizeof("run-test"));
  if (status != ZX_OK) {
    fprintf(stderr, "FAILURE: set_property() returned %d\n", status);
    return std::make_unique<Result>(test_name, FAILED_TO_LAUNCH, 0, 0);
  }

  // The TEST_ROOT_DIR environment variable allows tests that could be stored in
  // "/system" or "/boot" to discern where they are running, and modify paths
  // accordingly.
  //
  // TODO(fxbug.dev/3260): The hard-coded set of prefixes is not ideal. Ideally, this
  // would instead set the "root" to the parent directory of the "test/"
  // subdirectory where globbing was done to collect the set of tests in
  // DiscoverAndRunTests().  But then it's not clear what should happen if
  // using `-f` to provide a list of paths instead of directories to glob.
  const fbl::String root = RootName(path);
  // |root_var| must be kept alive for |env_vars| since |env_vars| may hold
  // a pointer into it.
  fbl::String root_var;
  fbl::Vector<const char*> env_vars;
  if (root == "/system" || root == "/boot") {
    for (size_t i = 0; environ[i] != nullptr; ++i) {
      env_vars.push_back(environ[i]);
    }
    root_var = fbl::String::Concat({"TEST_ROOT_DIR=", root});
    env_vars.push_back(root_var.c_str());
    env_vars.push_back(nullptr);
  }
  const char* const* env_vars_p = !env_vars.is_empty() ? env_vars.begin() : nullptr;

  zx::process process;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  const zx::time start_time = zx::clock::get_monotonic();

  status =
      fdio_spawn_etc(test_job.get(), fdio_flags, args[0], args, env_vars_p, fdio_actions.size(),
                     fdio_actions.data(), process.reset_and_get_address(), err_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "FAILURE: Failed to launch %s: %d (%s): %s\n", test_name, status,
            zx_status_get_string(status), err_msg);
    return std::make_unique<Result>(test_name, FAILED_TO_LAUNCH, 0, 0);
  }

  zx::time deadline = zx::time::infinite();
  if (timeout_msec) {
    deadline = zx::deadline_after(zx::msec(timeout_msec));
  }

  // Run the loop until the process terminates. Until the process terminates, asynchronously
  // handle any debug data that becomes ready.
  status = RunLoopUntilSignalOrDeadline(loop, deadline, process.get(), ZX_PROCESS_TERMINATED);
  const zx::time end_time = zx::clock::get_monotonic();
  const int64_t duration_milliseconds = (end_time - start_time).to_msecs();
  if (status != ZX_OK) {
    if (status == ZX_ERR_TIMED_OUT) {
      fprintf(stderr, "%s timed out\n", test_name);
      return std::make_unique<Result>(test_name, TIMED_OUT, 0, duration_milliseconds);
    }
    fprintf(stderr, "FAILURE: Failed to wait for process exiting %s: %d (%s)\n", test_name, status,
            zx_status_get_string(status));
    return std::make_unique<Result>(test_name, FAILED_TO_WAIT, 0, duration_milliseconds);
  }

  // Read the return code.
  zx_info_process_t proc_info;
  status = process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr);

  if (status != ZX_OK) {
    fprintf(stderr, "FAILURE: Failed to get process return code %s: %d\n", test_name, status);
    return std::make_unique<Result>(test_name, FAILED_TO_RETURN_CODE, 0, duration_milliseconds);
  }

  // Make a best effort to wait for any other tasks in the loop to terminate.
  auto_call_kill_job.call();
  status = RunLoopUntilSignalOrDeadline(loop, deadline, test_job.get(), ZX_TASK_TERMINATED);
  if (status != ZX_OK) {
    if (status == ZX_ERR_TIMED_OUT) {
      fprintf(stderr, "WARNING: Timed out waiting for test job to terminate\n");
    } else {
      fprintf(stderr, "WARNING: Failed to wait for job to terminate: %d (%s)\n", status,
              zx_status_get_string(status));
    }
  }

  // Run one more time until there are no unprocessed messages.
  loop.ResetQuit();
  loop.Run(zx::time(0));

  // Tear down the the VFS.
  vfs.reset();

  // The emitted signature, eg "[runtests][PASSED] /test/name", is used by the CQ/CI testrunners to
  // match test names and outcomes. Changes to this format must be matched in
  // https://fuchsia.googlesource.com/fuchsia/+/HEAD/tools/testing/runtests/output.go
  std::unique_ptr<Result> result;
  if (proc_info.return_code == 0) {
    result = std::make_unique<Result>(test_name, SUCCESS, 0, duration_milliseconds);
    if (data_collection_err_occurred) {
      result->launch_status = FAILED_COLLECTING_SINK_DATA;
    }
  } else {
    fprintf(stderr, "%s exited with nonzero status: %" PRId64, test_name, proc_info.return_code);
    result = std::make_unique<Result>(test_name, FAILED_NONZERO_RETURN_CODE, proc_info.return_code,
                                      duration_milliseconds);
  }
  if (debug_data_publisher) {
    debug_data_publisher->DrainData();
    auto written_files = debug_data_sink->FlushToDirectory(on_data_collection_error_callback,
                                                           on_data_collection_warning_callback);
    for (auto& [data_sink, files] : written_files) {
      std::vector<debugdata::DumpFile> vec_files;
      for (auto& [dump_file, tags] : files) {
        vec_files.push_back(dump_file);
      }
      result->data_sinks.emplace(data_sink, std::move(vec_files));
    }
  }

  return result;
}

}  // namespace runtests
