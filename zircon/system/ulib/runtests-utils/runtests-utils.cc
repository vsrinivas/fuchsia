// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/string_piece.h>
#include <fbl/string_printf.h>
#include <fbl/vector.h>
#include <runtests-utils/runtests-utils.h>

namespace runtests {

void ParseTestNames(const fbl::StringPiece input, fbl::Vector<fbl::String>* output) {
  // strsep modifies its input, so we have to make a mutable copy.
  // +1 because StringPiece::size() excludes null terminator.
  std::unique_ptr<char[]> input_copy(new char[input.size() + 1]);
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

int WriteSummaryJSON(const fbl::Vector<std::unique_ptr<Result>>& results,
                     const fbl::StringPiece output_file_basename,
                     const fbl::StringPiece syslog_path, FILE* summary_json) {
  int test_count = 0;
  fprintf(summary_json, "{\n  \"tests\": [\n");
  for (const auto& result : results) {
    if (test_count != 0) {
      fprintf(summary_json, ",\n");
    }
    fprintf(summary_json, "    {\n");

    // Write the name of the test.
    fprintf(summary_json, "      \"name\": \"%s\",\n", result->name.c_str());

    // Write the path to the output file, relative to the test output root
    // (i.e. what's passed in via -o). The test name is already a path to
    // the test binary on the target, so to make this a relative path, we
    // only have to skip leading '/' characters in the test name.
    fbl::String output_file = runtests::JoinPath(result->name, output_file_basename);
    size_t i = strspn(output_file.c_str(), "/");
    if (i == output_file.size()) {
      fprintf(stderr, "Error: output_file was empty or all slashes: %s\n", output_file.c_str());
      return EINVAL;
    }
    fprintf(summary_json, "      \"output_file\": \"%s\",\n", &(output_file.c_str()[i]));

    // Write the result of the test, which is either PASS or FAIL. We only
    // have one PASS condition in TestResult, which is SUCCESS.
    fprintf(summary_json, "      \"result\": \"%s\",\n",
            result->launch_status == runtests::SUCCESS ? "PASS" : "FAIL");

    fprintf(summary_json, "      \"duration_milliseconds\": %" PRId64,
            result->duration_milliseconds);

    // Write all data sinks.
    if (result->data_sinks.size()) {
      fprintf(summary_json, ",\n      \"data_sinks\": {\n");
      int sink_count = 0;
      for (const auto& sink : result->data_sinks) {
        if (sink_count != 0) {
          fprintf(summary_json, ",\n");
        }
        fprintf(summary_json, "        \"%s\": [\n", sink.first.c_str());
        int file_count = 0;
        for (const auto& file : sink.second) {
          if (file_count != 0) {
            fprintf(summary_json, ",\n");
          }
          fprintf(summary_json,
                  "          {\n"
                  "            \"name\": \"%s\",\n"
                  "            \"file\": \"%s\"\n"
                  "          }",
                  file.name.c_str(), file.file.c_str());
          file_count++;
        }
        fprintf(summary_json, "\n        ]");
        sink_count++;
      }
      fprintf(summary_json, "\n      }\n");
    } else {
      fprintf(summary_json, "\n");
    }
    fprintf(summary_json, "    }");
    test_count++;
  }
  fprintf(summary_json, "\n  ]");
  if (!syslog_path.empty()) {
    fprintf(summary_json,
            ",\n"
            "  \"outputs\": {\n"
            "    \"syslog_file\": \"%.*s\"\n"
            "  }\n",
            static_cast<int>(syslog_path.length()), syslog_path.data());
  } else {
    fprintf(summary_json, "\n");
  }
  // We really should have been checking for errors all along, but most likely the final fprintf
  // will fail or succeed along with all the others.
  const int final_ret_val = fprintf(summary_json, "}\n");
  if (final_ret_val < 0) {
    return errno;
  }
  return 0;
}

int ResolveGlobs(const fbl::Vector<fbl::String>& globs, fbl::Vector<fbl::String>* resolved) {
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

bool IsSharedLibraryName(fbl::StringPiece filename) {
  struct ExcludePattern {
    fbl::StringPiece prefix;
    fbl::StringPiece suffix;
  };
  static const fbl::Vector<ExcludePattern> kExcludePatterns = {
      {"lib", ".so"},
      {"lib", ".dylib"},
  };

  for (const auto& exclusion : kExcludePatterns) {
    if (filename.length() < exclusion.prefix.length() + exclusion.suffix.length()) {
      continue;
    }

    fbl::StringPiece start(filename.begin(), exclusion.prefix.length());
    fbl::StringPiece finish(filename.end() - exclusion.suffix.length(), exclusion.suffix.length());
    if (start == exclusion.prefix && finish == exclusion.suffix) {
      return true;
    }
  }

  return false;
}

int DiscoverTestsInDirGlobs(const fbl::Vector<fbl::String>& dir_globs, const char* ignore_dir_name,
                            const fbl::Vector<fbl::String>& basename_whitelist,
                            fbl::Vector<fbl::String>* test_paths) {
  fbl::Vector<fbl::String> test_dirs;
  const int err = ResolveGlobs(dir_globs, &test_dirs);
  if (err) {
    fprintf(stderr, "Error: Failed to resolve globs, error = %d\n", err);
    return EIO;  // glob()'s return values aren't the same as errno. This is somewhat arbitrary.
  }
  for (const fbl::String& test_dir : test_dirs) {
    // In the event of failures around a directory not existing or being an empty node
    // we will continue to the next entries rather than aborting. This allows us to handle
    // different sets of default test directories.
    struct stat st;
    if (stat(test_dir.c_str(), &st) < 0) {
      printf("Could not stat %s, skipping...\n", test_dir.c_str());
      continue;
    }
    if (!S_ISDIR(st.st_mode)) {
      // Silently skip non-directories, as they may have been picked up in
      // the glob.
      continue;
    }

    // Resolve an absolute path to the test directory to ensure output
    // directory names will never collide.
    char abs_test_dir[PATH_MAX];
    if (realpath(test_dir.c_str(), abs_test_dir) == nullptr) {
      printf("Error: Could not resolve path %s: %s\n", test_dir.c_str(), strerror(errno));
      continue;
    }

    // Silently skip |ignore_dir_name|.
    // The user may have done something like runtests /foo/bar/h*.
    const auto test_dir_base = basename(abs_test_dir);
    if (ignore_dir_name && strcmp(test_dir_base, ignore_dir_name) == 0) {
      continue;
    }

    DIR* dir = opendir(abs_test_dir);
    if (dir == nullptr) {
      fprintf(stderr, "Error: Could not open test dir %s\n", abs_test_dir);
      return errno;
    }

    struct dirent* de;
    while ((de = readdir(dir)) != nullptr) {
      fbl::StringPiece test_name = de->d_name;
      if (!basename_whitelist.is_empty() &&
          !runtests::IsInWhitelist(test_name, basename_whitelist)) {
        continue;
      }

      if (IsSharedLibraryName(test_name)) {
        continue;
      }

      const fbl::String test_path = runtests::JoinPath(abs_test_dir, test_name);
      if (stat(test_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        continue;
      }
      test_paths->push_back(test_path);
    }
    closedir(dir);
  }
  return 0;
}

bool RunTests(const fbl::Vector<fbl::String>& test_paths, const fbl::Vector<fbl::String>& test_args,
              int repeat, uint64_t timeout_msec, const char* output_dir,
              const fbl::StringPiece output_file_basename, int* failed_count,
              fbl::Vector<std::unique_ptr<Result>>* results) {
  std::map<fbl::String, int> test_name_to_count;
  for (int i = 1; i <= repeat; ++i) {
    for (const fbl::String& test_path : test_paths) {
      fbl::String output_filename_str;
      fbl::String output_test_name;
      if (test_name_to_count.find(test_path) != test_name_to_count.end()) {
        int count = test_name_to_count[test_path];
        count += 1;
        output_test_name = fbl::String::Concat({test_path, " (", std::to_string(count), ")"});
        test_name_to_count[test_path] = count;
      } else {
        test_name_to_count[test_path] = 1;
        output_test_name = test_path;
      }
      // Ensure the output directory for this test binary's output exists.
      if (output_dir != nullptr) {
        // If output_dir was specified, ask |RunTest| to redirect stdout/stderr
        // to a file whose name is based on the test name.
        fbl::String output_dir_for_test_str = runtests::JoinPath(output_dir, output_test_name);
        const int error = runtests::MkDirAll(output_dir_for_test_str);
        if (error) {
          fprintf(stderr, "Error: Could not create output directory %s: %s\n",
                  output_dir_for_test_str.c_str(), strerror(error));
          return false;
        }
        output_filename_str = JoinPath(output_dir_for_test_str, output_file_basename);
      }

      // Assemble test binary args.
      fbl::Vector<const char*> argv;
      argv.push_back(test_path.c_str());
      argv.reserve(test_args.size());
      for (auto test_arg = test_args.begin(); test_arg != test_args.end(); ++test_arg) {
        argv.push_back(test_arg->c_str());
      }
      argv.push_back(nullptr);  // Important, since there's no argc.

      const char* output_filename = nullptr;
      if (!output_filename_str.empty()) {
        output_filename = output_filename_str.c_str();
        // Ensure the output file exists, even if RunTest fails to create it.
        // This makes it possible for the infra to trust the summary.json.
        FILE* output_file = fopen(output_filename, "w");
        if (output_file == nullptr) {
          fprintf(stderr, "Error: Could not create output file %s: %s\n", output_filename,
                  strerror(errno));
          return false;
        }
        fclose(output_file);
      }

      // Execute the test binary.
      printf(
          "\n------------------------------------------------\n"
          "RUNNING TEST: %s\n\n",
          output_test_name.c_str());
      fflush(stdout);
      std::unique_ptr<Result> result =
          RunTest(argv.data(), output_dir, output_filename, output_test_name.c_str(), timeout_msec);
      if (result->launch_status != SUCCESS) {
        *failed_count += 1;
      }
      results->push_back(std::move(result));
    }

    if (i < repeat) {
      printf("\nPROGRESS: Ran %lu tests: %d failed\n", results->size(), *failed_count);
    }
  }
  return true;
}

bool IsFuchsiaPkgURI(const char* s) {
  const char prefix[] = "fuchsia-pkg://";
  const size_t prefix_len = sizeof(prefix) - 1;
  return 0 == strncmp(s, prefix, prefix_len);
}

}  // namespace runtests
