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

namespace runtests {

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

int WriteSummaryJSON(const fbl::Vector<fbl::unique_ptr<Result>>& results,
                     const fbl::StringPiece output_file_basename,
                     const fbl::StringPiece syslog_path,
                     FILE* summary_json) {
    int test_count = 0;
    fprintf(summary_json, "{\"tests\":[\n");
    for (const fbl::unique_ptr<Result>& result : results) {
        if (test_count != 0) {
            fprintf(summary_json, ",\n");
        }
        fprintf(summary_json, "{");

        // Write the name of the test.
        fprintf(summary_json, "\"name\":\"%s\"", result->name.c_str());

        // Write the path to the output file, relative to the test output root
        // (i.e. what's passed in via -o). The test name is already a path to
        // the test binary on the target, so to make this a relative path, we
        // only have to skip leading '/' characters in the test name.
        fbl::String output_file = runtests::JoinPath(result->name, output_file_basename);
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
                result->launch_status == runtests::SUCCESS ? "PASS" : "FAIL");

        fprintf(summary_json, "}");
        test_count++;
    }
    fprintf(summary_json, "\n]");
    if (!syslog_path.empty()) {
        fprintf(summary_json, ",\n\"outputs\":{\n");
        fprintf(summary_json, "\"syslog_file\":\"%.*s\"",
                static_cast<int>(syslog_path.length()),
                syslog_path.data());
        fprintf(summary_json, "\n}");
    }
    fprintf(summary_json, "}\n");
    return 0;
}

int ResolveGlobs(const fbl::Vector<fbl::String>& globs,
                 fbl::Vector<fbl::String>* resolved) {
    glob_t resolved_glob;
    auto auto_call_glob_free = fbl::MakeAutoCall([&resolved_glob] { globfree(&resolved_glob); });
    int flags = 0;
    for (const auto& test_dir_glob : globs) {
        int err = glob(test_dir_glob.c_str(), flags, nullptr, &resolved_glob);

        // Ignore a lack of matches.
        if (err && err != GLOB_NOMATCH) {
            return err;
        }
        flags = GLOB_APPEND;
    }
    resolved->reserve(resolved_glob.gl_pathc);
    for (size_t i = 0; i < resolved_glob.gl_pathc; ++i) {
        resolved->push_back(fbl::String(resolved_glob.gl_pathv[i]));
    }
    return 0;
}

bool RunTestsInDir(const RunTestFn& RunTest, const fbl::StringPiece dir_path,
                   const fbl::Vector<fbl::String>& filter_names,
                   const char* output_dir, const char* output_file_basename,
                   const signed char verbosity, int* num_failed,
                   fbl::Vector<fbl::unique_ptr<Result>>* results) {
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

    // max value for a signed char is 127, so 2 chars for "v=", 3 for integer, 1
    // for terminator.
    char verbosity_arg[6];
    snprintf(verbosity_arg, sizeof(verbosity_arg), "v=%d", verbosity);

    struct dirent* de;
    struct stat stat_buf;
    int failed_count = 0;

    // Iterate over the files in dir, setting up the output for test binaries
    // and executing them via run_test as they're found. Skips over test binaries
    // whose names aren't in filter_names.
    //
    // TODO(mknyszek): Iterate over these dirents (or just discovered test
    // binaries) in a deterministic order.
    while ((de = readdir(dir)) != nullptr) {
        const char* test_name = de->d_name;
        if (!filter_names.is_empty() &&
            !runtests::IsInWhitelist(test_name, filter_names)) {
            continue;
        }

        const fbl::String test_path = runtests::JoinPath(dir_path, test_name);
        if (stat(test_path.c_str(), &stat_buf) != 0 || !S_ISREG(stat_buf.st_mode)) {
            continue;
        }

        if (verbosity > 0) {
            printf(
                "\n------------------------------------------------\n"
                "RUNNING TEST: %s\n\n",
                test_name);
        }

        // If output_dir was specified, ask |RunTest| to redirect stdout/stderr
        // to a file whose name is based on the test name.
        fbl::String output_filename_str;
        if (output_dir != nullptr) {
            const fbl::String test_output_dir =
                runtests::JoinPath(output_dir, test_path);
            const int error = runtests::MkDirAll(test_output_dir);
            if (error) {
                printf("Error: Could not output directory for test %s: %s\n", test_name,
                       strerror(error));
                return false;
            }
            output_filename_str = JoinPath(test_output_dir, output_file_basename);
        }

        // Execute the test binary.
        const char* argv1 = (verbosity >= 0)? verbosity_arg : nullptr;
        const char* argv[] = {test_path.c_str(), argv1, nullptr};
        const char* output_filename =
            output_filename_str.empty() ? nullptr : output_filename_str.c_str();
        fbl::unique_ptr<Result> result = RunTest(argv, output_filename);
        if (result->launch_status != runtests::SUCCESS) {
            failed_count++;
        }
        results->push_back(fbl::move(result));
    }
    closedir(dir);
    *num_failed = failed_count;
    return failed_count == 0;
}

} // namespace runtests
