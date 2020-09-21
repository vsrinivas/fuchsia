// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/debugdata/debugdata.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fzl/vmo-mapper.h>
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
#include <utility>

#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <fs/synchronous_vfs.h>
#include <runtests-utils/fuchsia-run-test.h>
#include <runtests-utils/profile.h>
#include <runtests-utils/service-proxy-dir.h>

namespace fio = ::llcpp::fuchsia::io;

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

std::error_code ReadFile(const fbl::unique_fd& fd, uint8_t* data, size_t size) {
  auto* buf = data;
  ssize_t count = size;
  off_t off = 0;
  while (count > 0) {
    ssize_t len = pread(fd.get(), buf, count, off);
    if (len <= 0) {
      return std::error_code{errno, std::generic_category()};
    }
    buf += len;
    count -= len;
    off += len;
  }
  return std::error_code{};
}

std::error_code WriteFile(const fbl::unique_fd& fd, const uint8_t* data, size_t size) {
  auto* buf = data;
  ssize_t count = size;
  off_t off = 0;
  while (count > 0) {
    ssize_t len = pwrite(fd.get(), buf, count, off);
    if (len <= 0) {
      return std::error_code{errno, std::generic_category()};
    }
    buf += len;
    count -= len;
    off += len;
  }
  return std::error_code{};
}

std::optional<std::string> GetVMOName(const zx::vmo& vmo) {
  char name[ZX_MAX_NAME_LEN];
  zx_status_t status = vmo.get_property(ZX_PROP_NAME, name, sizeof(name));
  if (status != ZX_OK || name[0] == '\0') {
    zx_info_handle_basic_t info;
    status = vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    if (status != ZX_OK) {
      return {};
    }
    snprintf(name, sizeof(name), "unnamed.%" PRIu64, info.koid);
  }
  return name;
}

// This function processes all raw profiles that were published via data sink
// in an efficient manner. Concretely, rather than writing each data sink into
// a separate file, it merges all profiles from the same binary into a single
// profile. First it groups all VMOs by name which uniquely identifies each
// binary. Then it merges together all VMOs for the same binary together with
// data that's already on the disk (if it exists). Finally it writes the data
// back to disk (or creates the file if necessary). This ensures that at the
// end, we have exactly one profile for each binary in total, and each profile
// is read and written at most once per call to ProcessProfiles.
std::optional<std::vector<DumpFile>> ProcessProfiles(const std::vector<zx::vmo>& data,
                                                     const fbl::unique_fd& data_sink_dir_fd) {
  zx_status_t status;

  if (mkdirat(data_sink_dir_fd.get(), kProfileSink, 0777) != 0 && errno != EEXIST) {
    fprintf(stderr, "FAILURE: cannot mkdir \"%s\" for data-sink: %s\n", kProfileSink,
            strerror(errno));
    return {};
  }
  fbl::unique_fd sink_dir_fd{openat(data_sink_dir_fd.get(), kProfileSink, O_RDONLY | O_DIRECTORY)};
  if (!sink_dir_fd) {
    fprintf(stderr, "FAILURE: cannot open data-sink directory \"%s\": %s\n", kProfileSink,
            strerror(errno));
    return {};
  }

  std::unordered_map<std::string, std::forward_list<std::reference_wrapper<const zx::vmo>>>
      profiles;
  std::vector<DumpFile> dump_files;

  // Group data by profile name. The name is a hash computed from profile metadata and
  // should be unique across all binaries (modulo hash collisions).
  for (const auto& vmo : data) {
    auto name = GetVMOName(vmo);
    if (!name) {
      fprintf(stderr, "FAILURE: Cannot get a name for the VMO\n");
      return {};
    }
    profiles[*name].push_front(std::cref(vmo));
  }

  for (auto& [name, vmos] : profiles) {
    fbl::unique_fd fd{openat(sink_dir_fd.get(), name.c_str(), O_RDWR | O_CREAT, 0666)};
    if (!fd) {
      fprintf(stderr, "FAILURE: Cannot open data-sink file \"%s\": %s\n", name.c_str(),
              strerror(errno));
      return {};
    }

    uint64_t buffer_size;
    std::unique_ptr<uint8_t[]> buffer;

    struct stat stat;
    if (fstat(fd.get(), &stat) == -1) {
      fprintf(stderr, "FAILURE: Cannot stat data-sink file \"%s\": %s\n", name.c_str(),
              strerror(errno));
      return {};
    }
    if (auto file_size = static_cast<uint64_t>(stat.st_size); file_size > 0) {
      // The file already exists, use it to initialize the buffer...
      buffer_size = file_size;
      buffer = std::make_unique<uint8_t[]>(buffer_size);
      if (std::error_code ec = ReadFile(fd, buffer.get(), file_size); ec) {
        fprintf(stderr, "FAILURE: Cannot read data from \"%s\": %s\n", name.c_str(),
                strerror(ec.value()));
        return {};
      }
    }

    while (!vmos.empty()) {
      // Merge all VMOs into the buffer.
      const zx::vmo& vmo = vmos.front();
      vmos.pop_front();

      uint64_t vmo_size;
      status = vmo.get_size(&vmo_size);
      if (status != ZX_OK) {
        fprintf(stderr, "FAILURE: Cannot get size of VMO \"%s\" for data-sink \"%s\": %s\n",
                name.c_str(), kProfileSink, zx_status_get_string(status));
        return {};
      }

      fzl::VmoMapper mapper;
      if (vmo_size > 0) {
        zx_status_t status = mapper.Map(vmo, 0, vmo_size, ZX_VM_PERM_READ);
        if (status != ZX_OK) {
          fprintf(stderr, "FAILURE: Cannot map VMO \"%s\" for data-sink \"%s\": %s\n", name.c_str(),
                  kProfileSink, zx_status_get_string(status));
          return {};
        }
      } else {
        fprintf(stderr, "WARNING: Empty VMO \"%s\" published for data-sink \"%s\"\n", kProfileSink,
                name.c_str());
        continue;
      }

      if (likely(buffer)) {
        if (buffer_size != vmo_size) {
          fprintf(stderr, "FAILURE: Mismatch between content sizes for \"%s\": %lu != %lu\n",
                  name.c_str(), buffer_size, vmo_size);
        }
        ZX_ASSERT(buffer_size == vmo_size);

        // Ensure that profiles are structuraly compatible.
        if (!ProfilesCompatible(buffer.get(), reinterpret_cast<const uint8_t*>(mapper.start()),
                                buffer_size)) {
          fprintf(stderr, "WARNING: Unable to merge profile data: %s\n",
                  "source profile file is not compatible");
          continue;
        }

        MergeProfiles(buffer.get(), reinterpret_cast<const uint8_t*>(mapper.start()), buffer_size);
      } else {
        // ...Otherwise use the first non-empty VMO in the list to initialize the buffer.
        buffer_size = vmo_size;
        buffer = std::make_unique<uint8_t[]>(buffer_size);
        memcpy(buffer.get(), mapper.start(), buffer_size);
      }
    }

    // Write the data back to the file.
    if (std::error_code ec = WriteFile(fd, buffer.get(), buffer_size); ec) {
      fprintf(stderr, "FAILURE: Cannot write data to \"%s\": %s\n", name.c_str(),
              strerror(ec.value()));
      return {};
    }

    dump_files.push_back(DumpFile{name, JoinPath(kProfileSink, name).c_str()});
  }

  return dump_files;
}

std::optional<DumpFile> ProcessDataSinkDump(const std::string& sink_name, const zx::vmo& file_data,
                                            const fbl::unique_fd& data_sink_dir_fd) {
  zx_status_t status;

  if (mkdirat(data_sink_dir_fd.get(), sink_name.c_str(), 0777) != 0 && errno != EEXIST) {
    fprintf(stderr, "FAILURE: cannot mkdir \"%s\" for data-sink: %s\n", sink_name.c_str(),
            strerror(errno));
    return {};
  }
  fbl::unique_fd sink_dir_fd{
      openat(data_sink_dir_fd.get(), sink_name.c_str(), O_RDONLY | O_DIRECTORY)};
  if (!sink_dir_fd) {
    fprintf(stderr, "FAILURE: cannot open data-sink directory \"%s\": %s\n", sink_name.c_str(),
            strerror(errno));
    return {};
  }

  auto name = GetVMOName(file_data);
  if (!name) {
    fprintf(stderr, "FAILURE: Cannot get a name for the VMO\n");
    return {};
  }

  uint64_t size;
  status = file_data.get_size(&size);
  if (status != ZX_OK) {
    fprintf(stderr, "FAILURE: Cannot get size of VMO \"%s\" for data-sink \"%s\": %s\n",
            name->c_str(), sink_name.c_str(), zx_status_get_string(status));
    return {};
  }

  fzl::VmoMapper mapper;
  if (size > 0) {
    zx_status_t status = mapper.Map(file_data, 0, size, ZX_VM_PERM_READ);
    if (status != ZX_OK) {
      fprintf(stderr, "FAILURE: Cannot map VMO \"%s\" for data-sink \"%s\": %s\n", name->c_str(),
              sink_name.c_str(), zx_status_get_string(status));
      return {};
    }
  } else {
    fprintf(stderr, "WARNING: Empty VMO \"%s\" published for data-sink \"%s\"\n", name->c_str(),
            sink_name.c_str());
    return {};
  }

  zx_info_handle_basic_t info;
  status = file_data.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    fprintf(stderr, "FAILURE: Cannot get a basic info for VMO \"%s\": %s\n", name->c_str(),
            zx_status_get_string(status));
    return {};
  }

  char filename[ZX_MAX_NAME_LEN];
  snprintf(filename, sizeof(filename), "%s.%" PRIu64, sink_name.c_str(), info.koid);
  fbl::unique_fd fd{openat(sink_dir_fd.get(), filename, O_WRONLY | O_CREAT | O_EXCL, 0666)};
  if (!fd) {
    fprintf(stderr, "FAILURE: Cannot open data-sink file \"%s\": %s\n", filename, strerror(errno));
    return {};
  }
  if (std::error_code ec = WriteFile(fd, reinterpret_cast<uint8_t*>(mapper.start()), size); ec) {
    fprintf(stderr, "FAILURE: Cannot write data to \"%s\": %s\n", filename, strerror(ec.value()));
    return {};
  }

  return DumpFile{*name, JoinPath(sink_name, filename).c_str()};
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

std::unique_ptr<Result> RunTest(const char* argv[], const char* output_dir,
                                const char* output_filename, const char* test_name,
                                uint64_t timeout_msec) {
  // The arguments passed to fdio_spawn_etc. May be overridden.
  const char** args = argv;
  // calculate size of argv
  size_t argc = 0;
  while (argv[argc] != nullptr) {
    argc++;
  }

  const char* path = argv[0];
  fbl::String component_executor;

  if (!SetUpForTestComponent(path, &component_executor)) {
    return std::make_unique<Result>(path, FAILED_TO_LAUNCH, 0, 0);
  }

  const char* component_launch_args[argc + 3];
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
      fdio_spawn_action_t{.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = test_name_trunc}},
  };

  zx_status_t status;
  zx::channel svc_proxy_req;
  fbl::RefPtr<ServiceProxyDir> proxy_dir;
  std::unique_ptr<fs::SynchronousVfs> vfs;
  // This must be declared after the vfs so that its destructor gets called before the vfs
  // destructor. We do this explicitly at the end of the function in the non-error case, but in
  // error cases we just rely on the destructors to clean things up.
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  std::unique_ptr<debugdata::DebugData> debug_data;

  // Export the root namespace.
  fdio_flat_namespace_t* flat;
  if ((status = fdio_ns_export_root(&flat)) != ZX_OK) {
    fprintf(stderr, "FAILURE: Cannot export root namespace: %s\n", zx_status_get_string(status));
    return std::make_unique<Result>(path, FAILED_UNKNOWN, 0, 0);
  }
  auto auto_fdio_free_flat_ns = fbl::MakeAutoCall([&flat]() { fdio_ns_free_flat_ns(flat); });

  auto action_ns_entry = [](const char* prefix, zx_handle_t handle) {
    return fdio_spawn_action{.action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
                             .ns = {
                                 .prefix = prefix,
                                 .handle = handle,
                             }};
  };

  // If |output_dir| is provided, set up the loader and debugdata services that will be
  // used to capture any data published.
  if (output_dir != nullptr) {
    fbl::unique_fd root_dir_fd{open("/", O_RDONLY | O_DIRECTORY)};
    if (!root_dir_fd) {
      fprintf(stderr, "FAILURE: Could not open root directory /\n");
      return std::make_unique<Result>(path, FAILED_UNKNOWN, 0, 0);
    }

    zx::channel svc_proxy;
    status = zx::channel::create(0, &svc_proxy, &svc_proxy_req);
    if (status != ZX_OK) {
      fprintf(stderr, "FAILURE: Cannot create channel: %s\n", zx_status_get_string(status));
      return std::make_unique<Result>(path, FAILED_UNKNOWN, 0, 0);
    }

    zx::channel svc_handle;
    for (size_t i = 0; i < flat->count; ++i) {
      if (!strcmp(flat->path[i], "/svc")) {
        // Save the current /svc handle...
        svc_handle.reset(flat->handle[i]);
        // ...and replace it with the proxy /svc.
        fdio_actions.push_back(action_ns_entry("/svc", svc_proxy_req.get()));
      } else {
        fdio_actions.push_back(action_ns_entry(flat->path[i], flat->handle[i]));
      }
    }

    // Setup DebugData service implementation.
    debug_data = std::make_unique<debugdata::DebugData>(std::move(root_dir_fd));

    // Setup proxy dir.
    proxy_dir = fbl::MakeRefCounted<ServiceProxyDir>(std::move(svc_handle));
    auto node = fbl::MakeRefCounted<fs::Service>(
        [dispatcher = loop.dispatcher(), debug_data = debug_data.get()](zx::channel channel) {
          return fidl::BindSingleInFlightOnly(dispatcher, std::move(channel), debug_data);
        });
    proxy_dir->AddEntry(::llcpp::fuchsia::debugdata::DebugData::Name, node);

    // Setup VFS.
    vfs = std::make_unique<fs::SynchronousVfs>(loop.dispatcher());
    vfs->ServeDirectory(std::move(proxy_dir), std::move(svc_proxy), fs::Rights::ReadWrite());
    loop.StartThread();
  } else {
    for (size_t i = 0; i < flat->count; ++i) {
      fdio_actions.push_back(action_ns_entry(flat->path[i], flat->handle[i]));
    }
  }

  // If |output_filename| is provided, prepare the file descriptors that will
  // be used to tee the stdout/stderr of the test into the associated file.
  fbl::unique_fd fds[2];
  if (output_filename != nullptr) {
    int temp_fds[2] = {-1, -1};
    if (pipe(temp_fds)) {
      fprintf(stderr, "FAILURE: Failed to create pipe: %s\n", strerror(errno));
      return std::make_unique<Result>(test_name, FAILED_TO_LAUNCH, 0, 0);
    }
    fds[0].reset(temp_fds[0]);
    fds[1].reset(temp_fds[1]);

    fdio_actions.push_back(
        fdio_spawn_action{.action = FDIO_SPAWN_ACTION_CLONE_FD,
                          .fd = {.local_fd = fds[1].get(), .target_fd = STDOUT_FILENO}});
    fdio_actions.push_back(
        fdio_spawn_action{.action = FDIO_SPAWN_ACTION_TRANSFER_FD,
                          .fd = {.local_fd = fds[1].get(), .target_fd = STDERR_FILENO}});
  }
  zx::job test_job;
  status = zx::job::create(*zx::job::default_job(), 0, &test_job);
  if (status != ZX_OK) {
    fprintf(stderr, "FAILURE: zx::job::create() returned %d\n", status);
    return std::make_unique<Result>(test_name, FAILED_TO_LAUNCH, 0, 0);
  }
  auto auto_call_kill_job = fbl::MakeAutoCall([&test_job]() { test_job.kill(); });
  status = test_job.set_property(ZX_PROP_NAME, "run-test", sizeof("run-test"));
  if (status != ZX_OK) {
    fprintf(stderr, "FAILURE: set_property() returned %d\n", status);
    return std::make_unique<Result>(test_name, FAILED_TO_LAUNCH, 0, 0);
  }

  // The TEST_ROOT_DIR environment variable allows tests that could be stored in
  // "/system" or "/boot" to discern where they are running, and modify paths
  // accordingly.
  //
  // TODO(BLD-463): The hard-coded set of prefixes is not ideal. Ideally, this
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

  fds[1].release();  // To avoid double close since fdio_spawn_etc() closes it.
  zx::process process;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  const zx::time start_time = zx::clock::get_monotonic();

  status = fdio_spawn_etc(test_job.get(), FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_NAMESPACE,
                          args[0], args, env_vars_p, fdio_actions.size(), fdio_actions.data(),
                          process.reset_and_get_address(), err_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "FAILURE: Failed to launch %s: %d (%s): %s\n", test_name, status,
            zx_status_get_string(status), err_msg);
    return std::make_unique<Result>(test_name, FAILED_TO_LAUNCH, 0, 0);
  }

  zx::time deadline = zx::time::infinite();
  if (timeout_msec) {
    deadline = zx::deadline_after(zx::msec(timeout_msec));
  }

  // Tee output.
  if (output_filename != nullptr) {
    FILE* output_file = fopen(output_filename, "w");
    if (output_file == nullptr) {
      fprintf(stderr, "FAILURE: Could not open output file at %s: %s\n", output_filename,
              strerror(errno));
      return std::make_unique<Result>(test_name, FAILED_DURING_IO, 0, 0);
    }
    if (timeout_msec) {
      // If we have a timeout, we want non-blocking reads.
      // This will trigger the EAGAIN code path in the read loop.
      int flags = fcntl(fds[0].get(), F_GETFL, 0);
      fcntl(fds[0].get(), F_SETFL, flags | O_NONBLOCK);
    }
    char buf[1024];
    ssize_t bytes_read = 0;
    while ((bytes_read = read(fds[0].get(), buf, sizeof(buf))) != 0) {
      if (bytes_read > 0) {
        fwrite(buf, 1, bytes_read, output_file);
        fwrite(buf, 1, bytes_read, stdout);
      } else if (errno == EAGAIN) {
        const zx::time now = zx::clock::get_monotonic();
        if (now > deadline) {
          break;
        }
        const zx::duration sleep_for = std::min(zx::msec(100), deadline - now);
        zx::nanosleep(zx::deadline_after(sleep_for));
      } else {
        fprintf(stderr, "Failed to read test process' output: %s\n", strerror(errno));
        break;
      }
    }
    fflush(stdout);
    fflush(stderr);
    fflush(output_file);
    if (fclose(output_file)) {
      fprintf(stderr, "FAILURE: Could not close %s: %s\n", output_filename, strerror(errno));
      return std::make_unique<Result>(test_name, FAILED_DURING_IO, 0, 0);
    }
  }

  status = process.wait_one(ZX_PROCESS_TERMINATED, deadline, nullptr);
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

  // The emitted signature, eg "[runtests][PASSED] /test/name", is used by the CQ/CI testrunners to
  // match test names and outcomes. Changes to this format must be matched in
  // https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/tools/testing/runtests/output.go
  std::unique_ptr<Result> result;
  if (proc_info.return_code == 0) {
    result = std::make_unique<Result>(test_name, SUCCESS, 0, duration_milliseconds);
  } else {
    fprintf(stderr, "%s exited with nonzero status: %" PRId64, test_name, proc_info.return_code);
    result = std::make_unique<Result>(test_name, FAILED_NONZERO_RETURN_CODE, proc_info.return_code,
                                      duration_milliseconds);
  }

  if (output_dir == nullptr) {
    return result;
  }

  // Make sure that all job processes are dead before touching any data.
  auto_call_kill_job.call();

  // Stop the loop.
  loop.Quit();

  // Wait for any unfinished work to be completed.
  loop.JoinThreads();

  // Run one more time until there are no unprocessed messages.
  loop.ResetQuit();
  loop.Run(zx::time(0));

  // Tear down the the VFS.
  vfs.reset();

  fbl::unique_fd data_sink_dir_fd{open(output_dir, O_RDONLY | O_DIRECTORY)};
  if (!data_sink_dir_fd) {
    printf("FAILURE: Could not open output directory %s: %s\n", "/tmp", strerror(errno));
    return result;
  }

  for (const auto& [sink_name, data] : debug_data->data()) {
    if (sink_name == kProfileSink) {
      if (auto dump_files = ProcessProfiles(data, data_sink_dir_fd)) {
        result->data_sinks.emplace(sink_name, std::move(*dump_files));
      }
    } else {
      for (const auto& file_data : data) {
        if (auto dump_file = ProcessDataSinkDump(sink_name, file_data, data_sink_dir_fd)) {
          result->data_sinks[sink_name].push_back(*dump_file);
        } else if (result->return_code == 0) {
          result->launch_status = FAILED_COLLECTING_SINK_DATA;
        }
      }
    }
  }

  return result;
}

}  // namespace runtests
