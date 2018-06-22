// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helper functions for running test binaries and recording their results.

#ifndef ZIRCON_SYSTEM_ULIB_RUNTESTS_UTILS_INCLUDE_RUNTESTS_UTILS_RUNTESTS_UTILS_H_
#define ZIRCON_SYSTEM_ULIB_RUNTESTS_UTILS_INCLUDE_RUNTESTS_UTILS_RUNTESTS_UTILS_H_

#include <inttypes.h>

#include <fbl/string.h>
#include <fbl/string_piece.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>

namespace runtests {

// Status of launching a test subprocess.
enum LaunchStatus {
    SUCCESS,
    FAILED_TO_LAUNCH,
    FAILED_TO_WAIT,
    FAILED_DURING_IO,
    FAILED_TO_RETURN_CODE,
    FAILED_NONZERO_RETURN_CODE,
    FAILED_UNKNOWN,
};

// Represents the result of a single test run.
struct Result {
    fbl::String name; // argv[0].
    LaunchStatus launch_status;
    int64_t return_code; // Only valid if launch_status == SUCCESS or FAILED_NONZERO_RETURN_CODE.
    // TODO(ZX-2050): Track duration of test binary.

    // Constructor really only needed until we have C++14, which will allow call-sites to use
    // aggregate initializer syntax.
    Result(const char* name_arg, LaunchStatus launch_status_arg, int64_t return_code_arg)
        : name(name_arg), launch_status(launch_status_arg), return_code(return_code_arg) {}
};

// Function that invokes a test binary and writes its output to a file.
//
// |argv| is the commandline to use to run the test program; must be
//   null-terminated.
// |output_filename| is the name of the file to which the test binary's output
//   will be written. May be nullptr, in which case the output will not be
//   redirected.
typedef fbl::unique_ptr<Result> (*RunTestFn)(const char* argv[],
                                             const char* output_filename);

// A means of measuring how long it takes to run tests.
class Stopwatch {
public:
    virtual ~Stopwatch() = default;

    // Starts timing.
    virtual void Start() = 0;

    // Returns the elapsed time in milliseconds since invoking Start(), or else
    // since initialization if Start() has not yet been called.
    virtual int64_t DurationInMsecs() = 0;
};

// Splits |input| by ',' and appends the results onto |output|.
// Empty strings are not put into output.
void ParseTestNames(fbl::StringPiece input, fbl::Vector<fbl::String>* output);

// Returns true iff |name| is equal to one of strings in |whitelist|.
bool IsInWhitelist(fbl::StringPiece name, const fbl::Vector<fbl::String>& whitelist);

// Ensures |dir_name| exists by creating it and its parents if it doesn't.
// Returns 0 on success, else an error code compatible with errno.
int MkDirAll(fbl::StringPiece dir_name);

// Returns "|parent|/|child|". Unless child is absolute, in which case it returns |child|.
//
// |parent| is the parent path.
// |child| is the child path.
fbl::String JoinPath(fbl::StringPiece parent, fbl::StringPiece child);

// Writes a JSON summary of test results given a sequence of results.
//
// |results| are the run results to summarize.
// |output_file_basename| is base name of output file.
// |syslog_path| is the file path where syslogs are written.
// |summary_json| is the file stream to write the JSON summary to.
//
// Returns 0 on success, else an error code compatible with errno.
int WriteSummaryJSON(const fbl::Vector<fbl::unique_ptr<Result>>& results,
                     const fbl::StringPiece output_file_basename,
                     const fbl::StringPiece syslog_path,
                     FILE* summary_json);

// Resolves a set of globs.
//
// |globs| is an array of glob patterns.
// |resolved| will hold the results of resolving |globs|.
//
// Returns 0 on success, else an error code from glob.h.
int ResolveGlobs(const fbl::Vector<fbl::String>& globs,
                 fbl::Vector<fbl::String>* resolved);

// Executes all test binaries in a directory (non-recursive).
//
// |run_test| is the function used to invoke the test binaries.
// |dir_path| is the directory to search.
// |filter_names| is a list of test names to filter on (i.e. tests whose names
//   don't match are skipped). May be empty, in which case all tests will be run.
// |output_dir| is the output directory for all the tests' output. May be nullptr, in which case
//   output will not be captured.
// |output_file_basename| is the basename of the tests' output files. May be nullptr only if
//   |output_dir| is also nullptr.
//   Each test's standard output and standard error will be written to
//   |output_dir|/<test binary path>/|output_file_basename|.
// |verbosity| if > 0 is converted to a string and passed as an additional argument to the
//   tests, so argv = {test_path, "v=<verbosity>"}. Also if > 0, this function prints more output
//   to stdout than otherwise.
// |num_failed| is an output parameter which will be set to the number of test
//   binaries that failed.
// |results| is an output paramater to which run results will be appended.
//
// Returns false if any test binary failed, true otherwise.
bool RunTestsInDir(const RunTestFn& run_test, const fbl::StringPiece dir_path,
                   const fbl::Vector<fbl::String>& filter_names, const char* output_dir,
                   const char* output_file_basename, signed char verbosity,
                   int* num_failed, fbl::Vector<fbl::unique_ptr<Result>>* results);

// Conditionally runs all tests within given directories, with the option
// of writing an aggregated summary file.
//
// |RunTest|: function to run each test.
// |argc|: length of |argv|.
// |argv|: see //system/ulib/runtests-utils/run-all-tests.cpp,
//    specifically the 'Usage()' function, for documentation.
// |default_test_dirs|: directories in which to look for tests if no test
//    directory globs are specified.
// |stopwatch|: for timing how long all tests took to run.
// |syslog_file_name|: if an output directory is specified ("-o"), syslog ouput
//    will be written to a file under that directory and this name.
//
// Returns EXIT_SUCCESS if all tests passed; else, returns EXIT_FAILURE.
int RunAllTests(const RunTestFn& RunTest, int argc, const char* const* argv,
                const fbl::Vector<fbl::String>& default_test_dirs,
                Stopwatch* stopwatch, const fbl::StringPiece syslog_file_name);

} // namespace runtests

#endif // ZIRCON_SYSTEM_ULIB_RUNTESTS_UTILS_INCLUDE_RUNTESTS_UTILS_RUNTESTS_UTILS_H_
