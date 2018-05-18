// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtests-utils/runtests-utils.h>

#include <dirent.h>
#include <errno.h>
#include <glob.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/string_piece.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <fdio/util.h>
#include <launchpad/launchpad.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <runtests-utils/log-exporter.h>
#include <zircon/listnode.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

namespace runtests {

Result RunTest(const char* argv[], int argc, FILE* out) {
    int fds[2];
    const char* path = argv[0];

    launchpad_t* lp = nullptr;
    zx_status_t status = ZX_OK;

    zx::job test_job;
    status = zx::job::create(zx_job_default(), 0, &test_job);
    if (status != ZX_OK) {
        printf("FAILURE: zx::job::create() returned %d\n", status);
        return Result(path, FAILED_TO_LAUNCH, 0);
    }
    auto auto_call_kill_job = fbl::MakeAutoCall([&test_job]() { test_job.kill(); });
    auto auto_call_launchpad_destroy = fbl::MakeAutoCall([&lp]() {
        if (lp) {
            launchpad_destroy(lp);
        }
    });
    status = test_job.set_property(ZX_PROP_NAME, "run-test", sizeof("run-test"));
    if (status != ZX_OK) {
        printf("FAILURE: set_property() returned %d\n", status);
        return Result(path, FAILED_TO_LAUNCH, 0);
    }
    status = launchpad_create(test_job.get(), path, &lp);
    if (status != ZX_OK) {
        printf("FAILURE: launchpad_create() returned %d\n", status);
        return Result(path, FAILED_TO_LAUNCH, 0);
    }
    status = launchpad_load_from_file(lp, path);
    if (status != ZX_OK) {
        printf("FAILURE: launchpad_load_from_file() returned %d\n", status);
        return Result(path, FAILED_TO_LAUNCH, 0);
    }
    status = launchpad_clone(lp, LP_CLONE_FDIO_ALL | LP_CLONE_ENVIRON);
    if (status != ZX_OK) {
        printf("FAILURE: launchpad_clone() returned %d\n", status);
        return Result(path, FAILED_TO_LAUNCH, 0);
    }
    if (out != nullptr) {
        if (pipe(fds)) {
            printf("FAILURE: Failed to create pipe: %s\n", strerror(errno));
            return Result(path, FAILED_TO_LAUNCH, 0);
        }
        status = launchpad_clone_fd(lp, fds[1], STDOUT_FILENO);
        if (status != ZX_OK) {
            printf("FAILURE: launchpad_clone_fd() returned %d\n", status);
            return Result(path, FAILED_TO_LAUNCH, 0);
        }
        status = launchpad_transfer_fd(lp, fds[1], STDERR_FILENO);
        if (status != ZX_OK) {
            printf("FAILURE: launchpad_transfer_fd() returned %d\n", status);
            return Result(path, FAILED_TO_LAUNCH, 0);
        }
    }
    launchpad_set_args(lp, argc, argv);
    const char* errmsg;
    zx::process process;
    status = launchpad_go(lp, process.reset_and_get_address(), &errmsg);
    lp = nullptr; // launchpad_go destroys lp, null it so we don't try to destroy again.
    if (status != ZX_OK) {
        printf("FAILURE: Failed to launch %s: %d: %s\n", path, status, errmsg);
        return Result(path, FAILED_TO_LAUNCH, 0);
    }
    // Tee output.
    if (out != nullptr) {
        char buf[1024];
        ssize_t bytes_read = 0;
        while ((bytes_read = read(fds[0], buf, sizeof(buf))) > 0) {
            fwrite(buf, 1, bytes_read, out);
            fwrite(buf, 1, bytes_read, stdout);
        }
    }
    status = process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
    if (status != ZX_OK) {
        printf("FAILURE: Failed to wait for process exiting %s: %d\n", path, status);
        return Result(path, FAILED_TO_WAIT, 0);
    }

    // read the return code
    zx_info_process_t proc_info;
    status = process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr);

    if (status != ZX_OK) {
        printf("FAILURE: Failed to get process return code %s: %d\n", path, status);
        return Result(path, FAILED_TO_RETURN_CODE, 0);
    }

    if (proc_info.return_code != 0) {
        printf("FAILURE: %s exited with nonzero status: %" PRId64 "\n",
               path, proc_info.return_code);
        return Result(path, FAILED_NONZERO_RETURN_CODE, proc_info.return_code);
    }

    printf("PASSED: %s passed\n", path);
    return Result(path, SUCCESS, 0);
}

void ParseTestNames(const fbl::StringPiece input, fbl::Vector<fbl::String>* output) {
    // strsep modifies its input, so we have to make a mutable copy.
    // +1 because StringPiece::size() excludes null terminator.
    fbl::unique_ptr<char[]> input_copy(new char[input.size() + 1]);
    memcpy(input_copy.get(), input.data(), input.size());
    input_copy[input.size()] = '\0';

    // Tokenize the input string into names.
    char* next_token;
    for (char* tmp = strtok_r(input_copy.get(), ",", &next_token); tmp != nullptr;
         tmp = strtok_r(nullptr, ",", &next_token)) {
        output->push_back(fbl::String(tmp));
    }
}

bool IsInWhitelist(const fbl::StringPiece name, const fbl::Vector<fbl::String>& whitelist) {
    for (const fbl::String& whitelist_entry : whitelist) {
        if (name == fbl::StringPiece(whitelist_entry)) {
            return true;
        }
    }
    return false;
}

int MkDirAll(const fbl::StringPiece dir_name) {
    fbl::StringBuffer<PATH_MAX> dir_buf;
    if (dir_name.length() > dir_buf.capacity()) {
        return ENAMETOOLONG;
    }
    dir_buf.Append(dir_name);
    char* dir = dir_buf.data();

    // Fast path: check if the directory already exists.
    struct stat s;
    if (!stat(dir, &s)) {
        return 0;
    }

    // Slow path: create the directory and its parents.
    for (size_t slash = 0u; dir[slash]; slash++) {
        if (slash != 0u && dir[slash] == '/') {
            dir[slash] = '\0';
            if (mkdir(dir, 0755) && errno != EEXIST) {
                return false;
            }
            dir[slash] = '/';
        }
    }
    if (mkdir(dir, 0755) && errno != EEXIST) {
        return errno;
    }
    return 0;
}

fbl::String JoinPath(const fbl::StringPiece parent, const fbl::StringPiece child) {
    if (parent.empty()) {
        return fbl::String(child);
    }
    if (child.empty()) {
        return fbl::String(parent);
    }
    if (parent[parent.size() - 1] != '/' && child[0] != '/') {
        return fbl::String::Concat({parent, "/", child});
    }
    if (parent[parent.size() - 1] == '/' && child[0] == '/') {
        return fbl::String::Concat({parent, &child[1]});
    }
    return fbl::String::Concat({parent, child});
}

int WriteSummaryJSON(const fbl::Vector<Result>& results,
                     const fbl::StringPiece output_file_basename,
                     const fbl::StringPiece syslog_path,
                     FILE* summary_json) {
    int test_count = 0;
    fprintf(summary_json, "{\"tests\":[\n");
    for (const Result& result : results) {
        if (test_count != 0) {
            fprintf(summary_json, ",\n");
        }
        fprintf(summary_json, "{");

        // Write the name of the test.
        fprintf(summary_json, "\"name\":\"%s\"", result.name.c_str());

        // Write the path to the output file, relative to the test output root
        // (i.e. what's passed in via -o). The test name is already a path to
        // the test binary on the target, so to make this a relative path, we
        // only have to skip leading '/' characters in the test name.
        fbl::String output_file = runtests::JoinPath(result.name, output_file_basename);
        size_t i;
        for (i = 0; i < output_file.size() && output_file[i] == '/'; i++) {
        }
        if (i == output_file.size()) {
            printf("Error: output_file was empty or all slashes: %s\n", output_file.c_str());
            return EINVAL;
        }
        fprintf(summary_json, ",\"output_file\":\"%s\"", &(output_file.c_str()[i]));

        // Write the result of the test, which is either PASS or FAIL. We only
        // have one PASS condition in TestResult, which is SUCCESS.
        fprintf(summary_json, ",\"result\":\"%s\"",
                result.launch_status == runtests::SUCCESS ? "PASS" : "FAIL");

        fprintf(summary_json, "}");
        test_count++;
    }
    fprintf(summary_json, "\n],\n\"outputs\": {\n");
    fprintf(summary_json, "\"syslog_file\":\"%.*s\"", static_cast<int>(syslog_path.length()),
            syslog_path.data());
    fprintf(summary_json, "\n}}\n");
    return 0;
}

int ResolveGlobs(const char* const* globs, const int num_globs,
                 fbl::Vector<fbl::String>* resolved) {
    glob_t resolved_glob;
    auto auto_call_glob_free = fbl::MakeAutoCall([&resolved_glob] { globfree(&resolved_glob); });
    for (int i = 0; i < num_globs; i++) {
        const int flags = i > 0 ? GLOB_APPEND : 0;
        int err = glob(globs[i], flags, nullptr, &resolved_glob);

        // Ignore a lack of matches.
        if (err && err != GLOB_NOMATCH) {
            return err;
        }
    }
    resolved->reserve(resolved_glob.gl_pathc);
    for (size_t i = 0; i < resolved_glob.gl_pathc; ++i) {
        resolved->push_back(fbl::String(resolved_glob.gl_pathv[i]));
    }
    return 0;
}

bool RunTestsInDir(const fbl::StringPiece dir_path, const fbl::Vector<fbl::String>& filter_names,
                   const char* output_dir, const char* output_file_basename,
                   const signed char verbosity, int* num_failed, fbl::Vector<Result>* results) {
    if ((output_dir != nullptr) && (output_file_basename == nullptr)) {
        printf("Error: output_file_basename is not null, but output_dir is.\n");
        return false;
    }
    fbl::String dir_path_str = fbl::String(dir_path.data());
    DIR* dir = opendir(dir_path_str.c_str());
    if (dir == nullptr) {
        printf("Error: Could not open test dir %s\n", dir_path_str.c_str());
        return false;
    }

    // max value for a signed char is 127, so 2 chars for "v=", 3 for integer, 1 for terminator.
    char verbosity_arg[6];
    snprintf(verbosity_arg, sizeof(verbosity_arg), "v=%d", verbosity);
    const int argc = verbosity >= 0 ? 2 : 1;

    struct dirent* de;
    struct stat stat_buf;
    int failed_count = 0;

    // Iterate over the files in dir, setting up the output for test binaries
    // and executing them via run_test as they're found. Skips over test binaries
    // whose names aren't in filter_names.
    //
    // TODO(mknyszek): Iterate over these dirents (or just discovered test binaries)
    // in a deterministic order.
    while ((de = readdir(dir)) != nullptr) {
        const char* test_name = de->d_name;
        if (!filter_names.is_empty() && !runtests::IsInWhitelist(test_name, filter_names)) {
            continue;
        }

        const fbl::String test_path = runtests::JoinPath(dir_path, test_name);
        if (stat(test_path.c_str(), &stat_buf) != 0 || !S_ISREG(stat_buf.st_mode)) {
            continue;
        }

        if (verbosity > 0) {
            printf("\n------------------------------------------------\n"
                   "RUNNING TEST: %s\n\n",
                   test_name);
        }

        // If output_dir was specified, ask run_test to redirect stdout/stderr
        // to a file whose name is based on the test name.
        FILE* out = nullptr;
        if (output_dir != nullptr) {
            const fbl::String test_output_dir = runtests::JoinPath(output_dir, test_path);
            const int error = runtests::MkDirAll(test_output_dir);
            if (error) {
                printf("Error: Could not output directory for test %s: %s\n", test_name,
                       strerror(error));
                return false;
            }

            out = fopen(runtests::JoinPath(test_output_dir, output_file_basename).c_str(), "w");
            if (out == nullptr) {
                printf("Error: Could not open output file for test %s: %s\n", test_name,
                       strerror(errno));
                return false;
            }
        }

        // Execute the test binary.
        const char* argv[] = {test_path.c_str(), verbosity_arg};
        results->push_back(runtests::RunTest(argv, argc, out));
        if ((*results)[results->size() - 1].launch_status != runtests::SUCCESS) {
            failed_count++;
        }

        // Clean up the output file.
        if (out != nullptr && fclose(out)) {
            printf("FAILURE: Failed to close output file for test %s: %s\n", de->d_name,
                   strerror(errno));
            continue;
        }
    }

    closedir(dir);
    *num_failed = failed_count;
    return failed_count == 0;
}

fbl::unique_ptr<LogExporter> LaunchLogExporter(const fbl::StringPiece syslog_path,
                                               ExporterLaunchError* error) {
    *error = NO_ERROR;
    fbl::String syslog_path_str = fbl::String(syslog_path.data());
    FILE* syslog_file = fopen(syslog_path_str.c_str(), "w");
    if (syslog_file == nullptr) {
        printf("Error: Could not open syslog file: %s.\n", syslog_path_str.c_str());
        *error = OPEN_FILE;
        return nullptr;
    }

    // Try and connect to logger service if available. It would be only
    // available in garnet and above layer
    zx::channel logger, logger_request;
    zx_status_t status;

    status = zx::channel::create(0, &logger, &logger_request);
    if (status != ZX_OK) {
        printf("LaunchLogExporter: cannot create channel for logger service: %d (%s).\n",
               status, zx_status_get_string(status));
        *error = CREATE_CHANNEL;
        return nullptr;
    }

    status = fdio_service_connect("/svc/logger.Log", logger_request.release());
    if (status != ZX_OK) {
        printf("LaunchLogExporter: cannot connect to logger service: %d (%s).\n",
               status, zx_status_get_string(status));
        *error = CONNECT_TO_LOGGER_SERVICE;
        return nullptr;
    }

    // Create a log exporter channel and pass it to logger service.
    zx::channel listener, listener_request;
    status = zx::channel::create(0, &listener, &listener_request);
    if (status != ZX_OK) {
        printf("LaunchLogExporter: cannot create channel for listener: %d (%s).\n",
               status, zx_status_get_string(status));
        *error = CREATE_CHANNEL;
        return nullptr;
    }
    logger_LogListenRequest req = {};
    req.hdr.ordinal = logger_LogListenOrdinal;
    req.log_listener = FIDL_HANDLE_PRESENT;
    zx_handle_t listener_handle = listener.release();
    status = logger.write(0, &req, sizeof(req), &listener_handle, 1);
    if (status != ZX_OK) {
        printf("LaunchLogExporter: cannot pass listener to logger service: %d (%s).\n",
               status, zx_status_get_string(status));
        close(listener_handle);
        *error = FIDL_ERROR;
        return nullptr;
    }

    // Connect log exporter channel to object and start message loop on it.
    auto log_exporter = fbl::make_unique<LogExporter>(fbl::move(listener_request),
                                                      syslog_file);
    log_exporter->set_error_handler([](zx_status_t status) {
        if (status != ZX_ERR_CANCELED) {
            printf("log exporter: Failed: %d (%s).\n",
                   status, zx_status_get_string(status));
        }
    });
    log_exporter->set_file_error_handler([](const char* error) {
        printf("log exporter: Error writing to file: %s.\n", error);
    });
    status = log_exporter->StartThread();
    if (status != ZX_OK) {
        printf("LaunchLogExporter: Failed to start log exporter: %d (%s).\n",
               status, zx_status_get_string(status));
        *error = START_LISTENER;
        return nullptr;
    }
    return log_exporter;
}

} // namespace runtests
