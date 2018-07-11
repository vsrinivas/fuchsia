// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtests-utils/fuchsia-run-test.h>

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <loader-service/loader-service.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

namespace runtests {

// Path to helper binary which can run test as a component. This binary takes
// component url as its parameter.
constexpr char kRunTestComponentPath[] = "/system/bin/run_test_component";

fbl::String DirectoryName(const fbl::String& path) {
    char* cpath = strndup(path.data(), path.length());
    fbl::String ret(dirname(cpath));
    free(cpath);
    return ret;
}

fbl::String BaseName(const fbl::String& path) {
    char* cpath = strndup(path.data(), path.length());
    fbl::String ret(basename(cpath));
    free(cpath);
    return ret;
}

void TestFileComponentInfo(const fbl::String path,
                           fbl::String* component_url_out,
                           fbl::String* cmx_file_path_out) {
    if (strncmp(path.c_str(), kPkgPrefix, strlen(kPkgPrefix)) != 0) {
        return;
    }
    const auto folder_path = DirectoryName(DirectoryName(path));
    // folder_path should also start with |kPkgPrefix| and should not be equal
    // to |kPkgPrefix|.
    if (strncmp(folder_path.c_str(), kPkgPrefix, strlen(kPkgPrefix)) != 0 ||
        folder_path == fbl::String(kPkgPrefix)) {
        return;
    }

    const char* package_name = path.c_str() + strlen(kPkgPrefix);
    // find occurence of first '/'
    size_t i = 0;
    while (package_name[i] != '\0' && package_name[i] != '/') {
        i++;
    }
    const fbl::String package_name_str(package_name, i);
    const auto test_file_name = BaseName(path);
    *cmx_file_path_out = fbl::StringPrintf(
        "%s/meta/%s.cmx", folder_path.c_str(), test_file_name.c_str());
    *component_url_out =
        fbl::StringPrintf("fuchsia-pkg://fuchsia.com/%s#meta/%s.cmx",
                          package_name_str.c_str(), test_file_name.c_str());
}

namespace {

struct DataSinkDump {
    fbl::String sink_name;
    zx::vmo file_data;
};

struct LoaderServiceState {
    fbl::unique_fd root_dir_fd;
    fbl::Vector<DataSinkDump> data;
};

// This is a default set of library paths.
//
// Unfortunately this is duplicated in loader-service. We could get rid of this duplication by
// delegating to the existing loader service over FIDL for everything except PublishDataSink(),
// but the added complexity doesn't seem worth it.
const char* const kLibPaths[] = {"system/lib", "boot/lib"};

// This is a helper specifically for the C API boundary with the implementation code.
zx_status_t VmoFromFd(fbl::unique_fd fd, const char* file_name, zx_handle_t* out) {
    zx_status_t status = fdio_get_vmo_clone(fd.get(), out);
    if (status != ZX_OK) {
        return status;
    }
    return zx_object_set_property(*out, ZX_PROP_NAME, file_name, strlen(file_name));
}

zx_status_t LoadObject(void* ctx, const char* name, zx_handle_t* out) {
    const auto state = static_cast<LoaderServiceState*>(ctx);
    for (const auto libdir : kLibPaths) {
        char path[PATH_MAX];
        if (snprintf(path, sizeof(path), "%s/%s", libdir, name) < 0) {
            return ZX_ERR_INTERNAL;
        }
        fbl::unique_fd fd(openat(state->root_dir_fd.get(), path, O_RDONLY));
        if (fd) {
            return VmoFromFd(fbl::move(fd), name, out);
        }
    }
    return ZX_ERR_NOT_FOUND;
}

zx_status_t LoadAbspath(void* ctx, const char* path, zx_handle_t* out) {
    const auto state = static_cast<LoaderServiceState*>(ctx);
    fbl::unique_fd fd(openat(state->root_dir_fd.get(), path, O_RDONLY));
    if (fd) {
        return VmoFromFd(fbl::move(fd), path, out);
    }
    return ZX_ERR_NOT_FOUND;
}

zx_status_t PublishDataSink(void* ctx, const char* sink_name, zx_handle_t vmo) {
    const auto state = static_cast<LoaderServiceState*>(ctx);
    state->data.push_back({ sink_name, zx::vmo{vmo} });
    return ZX_OK;
}

void Finalizer(void* ctx) {
    const auto state = static_cast<LoaderServiceState*>(ctx);
    delete state;
}

const loader_service_ops_t fd_ops = {
    .load_object = LoadObject,
    .load_abspath = LoadAbspath,
    .publish_data_sink = PublishDataSink,
    .finalizer = Finalizer,
};

// To avoid creating a separate service thread for each test, we have a global
// instance of the async loop which is shared by all tests and their loader services.
fbl::unique_ptr<async::Loop> loop;

} // namespace

fbl::unique_ptr<Result> FuchsiaRunTest(const char* argv[],
                                       const char* output_dir,
                                       const char* output_filename) {
    // The arguments passed to fdio_spaws_etc. May be overridden.
    const char** args = argv;
    // calculate size of argv
    size_t argc = 0;
    while (argv[argc] != nullptr) {
        argc++;
    }

    // Values used when running the test as a component.
    const char* component_launch_args[argc + 2];
    fbl::String component_url;
    fbl::String cmx_file_path;

    const char* path = argv[0];

    TestFileComponentInfo(path, &component_url, &cmx_file_path);
    struct stat s;
    // If we get a non empty |cmx_file_path|, check that it exists, and if
    // present launch the test as component using generated |component_url|.
    if (cmx_file_path != "" && stat(cmx_file_path.c_str(), &s) == 0) {
        if (stat(kRunTestComponentPath, &s) != 0) {
            // TODO(anmittal): Make this a error once we have a stable
            // system and we can run all tests as components.
            fprintf(stderr,
                    "WARNING: Cannot find '%s', running '%s' as normal test "
                    "binary.",
                    kRunTestComponentPath, path);
        } else {
            component_launch_args[0] = kRunTestComponentPath;
            component_launch_args[1] = component_url.c_str();
            for (size_t i = 1; i <= argc; i++) {
                component_launch_args[1 + i] = argv[i];
            }
            args = component_launch_args;
        }
    }

    fbl::Vector<fdio_spawn_action_t> fdio_actions = {
        fdio_spawn_action_t{.action = FDIO_SPAWN_ACTION_SET_NAME,
          .name = {.data = path}},
    };

    LoaderServiceState* state = nullptr;
    zx_handle_t svc_handle = ZX_HANDLE_INVALID;
    loader_service_t* loader_service = nullptr;
    auto release_service = fbl::MakeAutoCall([&]() {
        if (loader_service != nullptr) {
            loader_service_release(loader_service);
        }
    });

    if (output_dir != nullptr) {
        state = new LoaderServiceState();
        // If |output_dir| is provided, set up the loader service that will be
        // used to capture any data published.
        state->root_dir_fd.reset(open("/", O_RDONLY | O_DIRECTORY));
        if (!state->root_dir_fd) {
            printf("FAILURE: Could not open root directory /\n");
            return fbl::make_unique<Result>(path, FAILED_UNKNOWN, 0);
        }

        if (!loop) {
            loop.reset(new async::Loop(&kAsyncLoopConfigNoAttachToThread));
            if (loop->StartThread("loader-service") != ZX_OK) {
                printf("FAILURE: cannot start message loop\n");
                loop.reset();
                return fbl::make_unique<Result>(path, FAILED_UNKNOWN, 0);
            }
        }

        if (loader_service_create(loop->dispatcher(), &fd_ops, state, &loader_service) != ZX_OK) {
            printf("FAILURE: cannot create loader service\n");
            delete state;
            return fbl::make_unique<Result>(path, FAILED_UNKNOWN, 0);
        }

        if (loader_service_connect(loader_service, &svc_handle) != ZX_OK) {
            printf("FAILURE: cannot connect loader service\n");
            return fbl::make_unique<Result>(path, FAILED_UNKNOWN, 0);
        }

        fdio_actions.push_back(
            fdio_spawn_action{.action = FDIO_SPAWN_ACTION_ADD_HANDLE,
             .h = {.id = PA_LDSVC_LOADER, .handle = svc_handle}});
    }

    // If |output_filename| is provided, prepare the file descriptors that will
    // be used to tee the stdout/stderr of the test into the associated file.
    fbl::unique_fd fds[2];
    if (output_filename != nullptr) {
        int temp_fds[2] = {-1, -1};
        if (pipe(temp_fds)) {
            fprintf(stderr, "FAILURE: Failed to create pipe: %s\n",
                    strerror(errno));
            return fbl::make_unique<Result>(path, FAILED_TO_LAUNCH, 0);
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
    zx_status_t status = ZX_OK;
    zx::job test_job;
    status = zx::job::create(*zx::job::default_job(), 0, &test_job);
    if (status != ZX_OK) {
        fprintf(stderr, "FAILURE: zx::job::create() returned %d\n", status);
        return fbl::make_unique<Result>(path, FAILED_TO_LAUNCH, 0);
    }
    auto auto_call_kill_job =
        fbl::MakeAutoCall([&test_job]() { test_job.kill(); });
    status =
        test_job.set_property(ZX_PROP_NAME, "run-test", sizeof("run-test"));
    if (status != ZX_OK) {
        fprintf(stderr, "FAILURE: set_property() returned %d\n", status);
        return fbl::make_unique<Result>(path, FAILED_TO_LAUNCH, 0);
    }

    fds[1].release(); // To avoid double close since fdio_spawn_etc() closes it.
    zx::process process;
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    status = fdio_spawn_etc(test_job.get(), FDIO_SPAWN_CLONE_ALL,
                            args[0], args, nullptr,
                            fdio_actions.size(), fdio_actions.get(),
                            process.reset_and_get_address(), err_msg);
    if (status != ZX_OK) {
        fprintf(stderr, "FAILURE: Failed to launch %s: %d (%s): %s\n", path,
                status, zx_status_get_string(status), err_msg);
        return fbl::make_unique<Result>(path, FAILED_TO_LAUNCH, 0);
    }
    // Tee output.
    if (output_filename != nullptr) {
        FILE* output_file = fopen(output_filename, "w");
        if (output_file == nullptr) {
            fprintf(stderr, "FAILURE: Could not open output file at %s: %s\n",
                    output_filename, strerror(errno));
            return fbl::make_unique<Result>(path, FAILED_DURING_IO, 0);
        }
        char buf[1024];
        ssize_t bytes_read = 0;
        while ((bytes_read = read(fds[0].get(), buf, sizeof(buf))) > 0) {
            fwrite(buf, 1, bytes_read, output_file);
            fwrite(buf, 1, bytes_read, stdout);
        }
        if (fclose(output_file)) {
            fprintf(stderr, "FAILURE:  Could not close %s: %s", output_filename,
                    strerror(errno));
            return fbl::make_unique<Result>(path, FAILED_DURING_IO, 0);
        }
    }
    status =
        process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
    if (status != ZX_OK) {
        fprintf(stderr, "FAILURE: Failed to wait for process exiting %s: %d\n",
                path, status);
        return fbl::make_unique<Result>(path, FAILED_TO_WAIT, 0);
    }

    // Read the return code.
    zx_info_process_t proc_info;
    status = process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info),
                              nullptr, nullptr);

    if (status != ZX_OK) {
        fprintf(stderr, "FAILURE: Failed to get process return code %s: %d\n",
                path, status);
        return fbl::make_unique<Result>(path, FAILED_TO_RETURN_CODE, 0);
    }

    fbl::unique_ptr<Result> result;
    if (proc_info.return_code == 0) {
        fprintf(stderr, "PASSED: %s passed\n", path);
        result = fbl::make_unique<Result>(path, SUCCESS, 0);
    } else {
        fprintf(stderr, "FAILURE: %s exited with nonzero status: %" PRId64 "\n",
                path, proc_info.return_code);
        result = fbl::make_unique<Result>(path, FAILED_NONZERO_RETURN_CODE,
                                          proc_info.return_code);
    }

    if (output_dir == nullptr) {
        return result;
    }

    // Make sure that all job processes are dead before touching any data.
    auto_call_kill_job.call();

    fbl::unique_fd data_sink_dir_fd{open(output_dir, O_RDONLY | O_DIRECTORY)};
    if (!data_sink_dir_fd) {
        printf("FAILURE: Could not open output directory %s: %s\n", output_dir,
               strerror(errno));
        return result;
    }

    for (auto& data : state->data) {
        if (mkdirat(data_sink_dir_fd.get(), data.sink_name.c_str(), 0777) != 0 && errno != EEXIST) {
            fprintf(stderr, "FAILURE: cannot mkdir \"%s\" for data-sink: %s\n",
                    data.sink_name.c_str(), strerror(errno));
            if (result->return_code == 0) {
                result->launch_status = FAILED_COLLECTING_SINK_DATA;
            }
            continue;
        }
        fbl::unique_fd sink_dir_fd{openat(data_sink_dir_fd.get(), data.sink_name.c_str(),
                                          O_RDONLY | O_DIRECTORY)};
        if (!sink_dir_fd) {
            fprintf(stderr, "FAILURE: cannot open data-sink directory \"%s\": %s\n",
                    data.sink_name.c_str(), strerror(errno));
            if (result->return_code == 0) {
                result->launch_status = FAILED_COLLECTING_SINK_DATA;
            }
            continue;
        }

        uint64_t size;
        zx_status_t status = data.file_data.get_size(&size);
        if (status != ZX_OK) {
            fprintf(stderr, "FAILURE: Cannot get VMO size for data-sink \"%s\": %s\n",
                    data.sink_name.c_str(), zx_status_get_string(status));
            result->launch_status = FAILED_COLLECTING_SINK_DATA;
            continue;
        }

        uintptr_t mapping;
        status = zx::vmar::root_self()->map(0, data.file_data, 0, size,
                                            ZX_VM_FLAG_PERM_READ, &mapping);
        if (status != ZX_OK) {
            fprintf(stderr, "FAILURE: Cannot map VMO of %" PRIu64 " for data-sink \"%s\": %s\n",
                    size, data.sink_name.c_str(), zx_status_get_string(status));
            if (result->return_code == 0) {
                result->launch_status = FAILED_COLLECTING_SINK_DATA;
            }
            continue;
        }
        auto unmap_vmar = fbl::MakeAutoCall([&]() { zx::vmar::root_self()->unmap(mapping, size); });

        zx_info_handle_basic_t info;
        status = data.file_data.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                                         nullptr, nullptr);
        if (status != ZX_OK) {
            if (result->return_code == 0) {
                result->launch_status = FAILED_COLLECTING_SINK_DATA;
            }
            continue;
        }
        char filename[ZX_MAX_NAME_LEN];
        snprintf(filename, sizeof(filename), "%s.%" PRIu64, data.sink_name.c_str(), info.koid);

        fbl::unique_fd fd{openat(sink_dir_fd.get(), filename, O_WRONLY | O_CREAT | O_EXCL, 0666)};
        if (!fd) {
            fprintf(stderr, "FAILURE: Cannot open data-sink file \"%s\": %s\n",
                    data.sink_name.c_str(), strerror(errno));
            if (result->return_code == 0) {
                result->launch_status = FAILED_COLLECTING_SINK_DATA;
            }
            continue;
        }
        // Strip any leading slashes (including a sequence of slashes) so the dump
        // file path is a relative to directory that contains the summary file.
        size_t i = strspn(path, "/");
        auto dump_file = JoinPath(&path[i], JoinPath(data.sink_name, filename));

        uint8_t *buf = reinterpret_cast<uint8_t *>(mapping);
        ssize_t count = size;
        ssize_t len = 0;
        while (count > 0 && (len = write(fd.get(), buf, count)) != count) {
            if (len == -1) {
                fprintf(stderr, "FAILURE: Cannot write data to \"%s\": %s\n",
                        dump_file.c_str(), strerror(errno));
                if (result->return_code == 0) {
                    result->launch_status = FAILED_COLLECTING_SINK_DATA;
                }
                break;
            }
            count -= len;
            buf += len;
        }
        if (len == -1) {
            continue;
        }

        char name[ZX_MAX_NAME_LEN];
        status = data.file_data.get_property(ZX_PROP_NAME, name, sizeof(name));
        if (status != ZX_OK) {
            if (result->return_code == 0) {
                result->launch_status = FAILED_COLLECTING_SINK_DATA;
            }
            continue;
        }
        if (name[0] == '\0') {
            snprintf(name, sizeof(name), "unnamed.%" PRIu64, info.koid);
        }

        Result::HashTable::iterator it;
        result->data_sinks.insert_or_find(fbl::make_unique<DataSink>(data.sink_name), &it);
        it->files.push_back(DumpFile{name, dump_file});
    }

    return result;
}

} // namespace runtests
