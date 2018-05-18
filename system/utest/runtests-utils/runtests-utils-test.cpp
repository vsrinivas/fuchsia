// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <limits.h>
#include <memfs/memfs.h>
#include <runtests-utils/runtests-utils.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unittest/unittest.h>

namespace runtests {
namespace {

static constexpr size_t kOneMegabyte = 1 << 20;
static constexpr char kMemFSPath[] = "/test-memfs";
static constexpr char kOutputFileName[] = "output.txt";
static constexpr char kEchoSuccessAndArgs[] = "echo Success! $@";
static constexpr char kEchoFailureAndArgs[] = "echo Failure!  $@ 1>&2\nexit 77";

// Creates a script file with given contents in constructor and deletes it in destructor.
class ScopedScriptFile {

public:
    // |path| is the path of the file to be created. Should start with kMemFsPath.
    // |contents| are the script contents. Shebang line will be added automatically.
    ScopedScriptFile(const fbl::StringPiece path, const fbl::StringPiece contents);
    ~ScopedScriptFile();
    fbl::StringPiece path() const;

private:
    const fbl::StringPiece path_;
};

ScopedScriptFile::ScopedScriptFile(
    const fbl::StringPiece path, const fbl::StringPiece contents)
    : path_(path) {
    const int fd = open(path_.data(), O_CREAT | O_WRONLY, S_IRWXU);
    ZX_ASSERT_MSG(-1 != fd, "%s", strerror(errno));
    const char she_bang[] = "#!/boot/bin/sh\n\n";
    ZX_ASSERT(sizeof(she_bang) == static_cast<size_t>(write(fd, she_bang, sizeof(she_bang))));
    ZX_ASSERT(contents.size() == static_cast<size_t>(write(fd, contents.data(), contents.size())));
    ZX_ASSERT_MSG(-1 != close(fd), "%s", strerror(errno));
}

ScopedScriptFile::~ScopedScriptFile() {
    remove(path_.data());
}

fbl::StringPiece ScopedScriptFile::path() const {
    return path_;
}

bool RunTestSuccess() {
    BEGIN_TEST;
    const char* argv[] = {"/test-memfs/succeed.sh"};
    ScopedScriptFile script(argv[0], "exit 0");
    const Result result = RunTest(argv, 1, nullptr);
    EXPECT_STR_EQ(argv[0], result.name.c_str());
    EXPECT_EQ(SUCCESS, result.launch_status);
    EXPECT_EQ(0, result.return_code);
    END_TEST;
}

bool RunTestSuccessWithStdout() {
    BEGIN_TEST;
    // A reasonable guess that the process won't output more than this.
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    FILE* buf_file = fmemopen(buf, sizeof(buf), "w");
    const char* argv[] = {"/test-memfs/succeed.sh"};
    const char expected_output[] = "Expect this!\n";
    // Produces expected_output, b/c echo adds newline
    const char script_contents[] = "echo Expect this!";
    ScopedScriptFile script(argv[0], script_contents);
    const Result result = RunTest(argv, 1, buf_file);
    fclose(buf_file);
    ASSERT_STR_EQ(expected_output, buf);
    EXPECT_STR_EQ(argv[0], result.name.c_str());
    EXPECT_EQ(SUCCESS, result.launch_status);
    EXPECT_EQ(0, result.return_code);
    END_TEST;
}

bool RunTestFailureWithStderr() {
    BEGIN_TEST;
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    FILE* buf_file = fmemopen(buf, sizeof(buf), "w");
    const char* argv[] = {"/test-memfs/fail.sh"};
    const char expected_output[] = "Expect this!\n";
    // Produces expected_output, b/c echo adds newline
    const char script_contents[] = "echo Expect this! 1>&2\nexit 77";
    ScopedScriptFile script(argv[0], script_contents);
    const Result result = RunTest(argv, 1, buf_file);
    fclose(buf_file);
    ASSERT_STR_EQ(expected_output, buf);
    EXPECT_STR_EQ(argv[0], result.name.c_str());
    EXPECT_EQ(FAILED_NONZERO_RETURN_CODE, result.launch_status);
    EXPECT_EQ(77, result.return_code);
    END_TEST;
}

bool RunTestFailureToLoadFile() {
    BEGIN_TEST;
    const char* argv[] = {"i/do/not/exist/"};
    const Result result = RunTest(argv, 1, nullptr);
    EXPECT_STR_EQ(argv[0], result.name.c_str());
    EXPECT_EQ(FAILED_TO_LAUNCH, result.launch_status);
    END_TEST;
}

bool ParseTestNamesEmptyStr() {
    BEGIN_TEST;
    fbl::String input("");
    fbl::Vector<fbl::String> parsed;
    ParseTestNames(input, &parsed);
    EXPECT_EQ(0, parsed.size());
    END_TEST;
}

bool ParseTestNamesEmptyStrInMiddle() {
    BEGIN_TEST;
    fbl::String input("a,,b");
    fbl::Vector<fbl::String> parsed;
    ParseTestNames(input, &parsed);
    ASSERT_EQ(2, parsed.size());
    EXPECT_STR_EQ("a", parsed[0].c_str());
    EXPECT_STR_EQ("b", parsed[1].c_str());
    END_TEST;
}

bool ParseTestNamesTrailingComma() {
    BEGIN_TEST;
    fbl::String input("a,");
    fbl::Vector<fbl::String> parsed;
    ParseTestNames(input, &parsed);
    ASSERT_EQ(1, parsed.size());
    EXPECT_STR_EQ("a", parsed[0].c_str());
    END_TEST;
}

bool ParseTestNamesNormal() {
    BEGIN_TEST;
    fbl::String input("a,b");
    fbl::Vector<fbl::String> parsed;
    ParseTestNames(input, &parsed);
    ASSERT_EQ(2, parsed.size());
    EXPECT_STR_EQ("a", parsed[0].c_str());
    EXPECT_STR_EQ("b", parsed[1].c_str());
    END_TEST;
}

bool EmptyWhitelist() {
    BEGIN_TEST;
    fbl::Vector<fbl::String> whitelist;
    EXPECT_FALSE(IsInWhitelist("a", whitelist));
    END_TEST;
}

bool NonemptyWhitelist() {
    BEGIN_TEST;
    fbl::Vector<fbl::String> whitelist = {"b", "a"};
    EXPECT_TRUE(IsInWhitelist("a", whitelist));
    END_TEST;
}

bool JoinPathNoTrailingSlash() {
    BEGIN_TEST;
    EXPECT_STR_EQ("a/b/c/d", JoinPath("a/b", "c/d").c_str());
    END_TEST;
}
bool JoinPathTrailingSlash() {
    BEGIN_TEST;
    EXPECT_STR_EQ("a/b/c/d", JoinPath("a/b/", "c/d").c_str());
    END_TEST;
}
bool JoinPathAbsoluteChild() {
    BEGIN_TEST;
    EXPECT_STR_EQ("a/b/c/d", JoinPath("a/b/", "/c/d").c_str());
    END_TEST;
}

bool MkDirAllTooLong() {
    BEGIN_TEST;
    char too_long[PATH_MAX + 2];
    memset(too_long, 'a', PATH_MAX + 1);
    too_long[PATH_MAX + 1] = '\0';
    EXPECT_EQ(ENAMETOOLONG, MkDirAll(too_long));
    END_TEST;
}
bool MkDirAllAlreadyExists() {
    BEGIN_TEST;
    const fbl::String already = JoinPath(kMemFSPath, "already");
    const fbl::String exists = JoinPath(already, "exists");
    ASSERT_EQ(0, mkdir(already.c_str(), 0755));
    ASSERT_EQ(0, mkdir(exists.c_str(), 0755));
    EXPECT_EQ(0, MkDirAll(exists));
    END_TEST;
}
bool MkDirAllParentAlreadyExists() {
    BEGIN_TEST;
    const fbl::String parent = JoinPath(kMemFSPath, "existing-parent");
    const fbl::String child = JoinPath(parent, "child");
    ASSERT_EQ(0, mkdir(parent.c_str(), 0755));
    EXPECT_EQ(0, MkDirAll(child));
    struct stat s;
    EXPECT_EQ(0, stat(child.c_str(), &s));
    END_TEST;
}
bool MkDirAllParentDoesNotExist() {
    BEGIN_TEST;
    const fbl::String parent = JoinPath(kMemFSPath, "not-existing-parent");
    const fbl::String child = JoinPath(parent, "child");
    struct stat s;
    ASSERT_NE(0, stat(parent.c_str(), &s));
    EXPECT_EQ(0, MkDirAll(child));
    EXPECT_EQ(0, stat(child.c_str(), &s));
    END_TEST;
}

bool WriteSummaryJSONSucceeds() {
    BEGIN_TEST;
    // A reasonable guess that the function won't output more than this.
    fbl::unique_ptr<char[]> buf(new char[kOneMegabyte]);
    FILE* buf_file = fmemopen(buf.get(), kOneMegabyte, "w");
    const fbl::Vector<Result> results = {Result("/a", SUCCESS, 0),
                                         Result("b", FAILED_TO_LAUNCH, 0)};
    ASSERT_EQ(0, WriteSummaryJSON(results, kOutputFileName, "/tmp/file_path", buf_file));
    fclose(buf_file);
    // We don't have a JSON parser in zircon right now, so just hard-code the expected output.
    const char kExpectedJSONOutput[] = R"({"tests":[
{"name":"/a","output_file":"a/output.txt","result":"PASS"},
{"name":"b","output_file":"b/output.txt","result":"FAIL"}
],
"outputs": {
"syslog_file":"/tmp/file_path"
}}
)";
    EXPECT_STR_EQ(kExpectedJSONOutput, buf.get());
    END_TEST;
}
bool WriteSummaryJSONBadTestName() {
    BEGIN_TEST;
    // A reasonable guess that the function won't output more than this.
    fbl::unique_ptr<char[]> buf(new char[kOneMegabyte]);
    FILE* buf_file = fmemopen(buf.get(), kOneMegabyte, "w");
    // A test name and output file consisting entirely of slashes should trigger an error.
    const fbl::Vector<Result> results = {Result("///", SUCCESS, 0),
                                         Result("b", FAILED_TO_LAUNCH, 0)};
    ASSERT_NE(0, WriteSummaryJSON(results, /*output_file_basename=*/"///", /*syslog_path=*/"/", buf_file));
    fclose(buf_file);
    END_TEST;
}

bool ResolveGlobsNoMatches() {
    BEGIN_TEST;
    fbl::Vector<fbl::String> resolved;
    const char* globs[] = {"/foo/bar/*", "/test-memfs/bar*"};
    ASSERT_EQ(0, ResolveGlobs(globs, sizeof(globs) / sizeof(globs[0]), &resolved));
    EXPECT_EQ(0, resolved.size());
    END_TEST;
}

bool ResolveGlobsMultipleMatches() {
    BEGIN_TEST;
    const char existing_dir_path[] = "/test-memfs/existing-dir/prefix-suffix";
    const char existing_file_path[] = "/test-memfs/existing-file";
    const char* globs[] = {"/does/not/exist/*",
                           "/test-memfs/existing-dir/prefix*", // matches existing_dir_path.
                           existing_file_path};
    ASSERT_EQ(0, MkDirAll(existing_dir_path));
    const int existing_file_fd = open(existing_file_path, O_CREAT);
    ASSERT_NE(-1, existing_file_fd, strerror(errno));
    ASSERT_NE(-1, close(existing_file_fd), strerror(errno));
    fbl::Vector<fbl::String> resolved;
    ASSERT_EQ(0, ResolveGlobs(globs, sizeof(globs) / sizeof(globs[0]), &resolved));
    ASSERT_EQ(2, resolved.size());
    EXPECT_STR_EQ(existing_dir_path, resolved[0].c_str());
    EXPECT_STR_EQ(existing_dir_path, resolved[0].c_str());
    END_TEST;
}

bool RunTestsInDirBasic() {
    BEGIN_TEST;
    ScopedScriptFile succeed_file("/test-memfs/succeed.sh", kEchoSuccessAndArgs);
    ScopedScriptFile fail_file("/test-memfs/fail.sh", kEchoFailureAndArgs);
    int num_failed;
    fbl::Vector<Result> results;
    const signed char verbosity = -1;
    EXPECT_FALSE(
        RunTestsInDir(kMemFSPath, {}, nullptr, nullptr, verbosity, &num_failed, &results));
    EXPECT_EQ(1, num_failed);
    EXPECT_EQ(2, results.size());
    bool found_succeed_result = false;
    bool found_fail_result = false;
    // The order of the results is not defined, so just check that each is present.
    for (const Result& result : results) {
        if (fbl::StringPiece(result.name) == succeed_file.path()) {
            found_succeed_result = true;
            EXPECT_EQ(SUCCESS, result.launch_status);
        } else if (fbl::StringPiece(result.name) == fail_file.path()) {
            found_fail_result = true;
            EXPECT_EQ(FAILED_NONZERO_RETURN_CODE, result.launch_status);
        }
    }
    EXPECT_TRUE(found_succeed_result);
    EXPECT_TRUE(found_fail_result);
    END_TEST;
}

bool RunTestsInDirFilter() {
    BEGIN_TEST;
    ScopedScriptFile succeed_file("/test-memfs/succeed.sh", kEchoSuccessAndArgs);
    ScopedScriptFile fail_file("/test-memfs/fail.sh", kEchoFailureAndArgs);
    int num_failed;
    fbl::Vector<Result> results;
    fbl::Vector<fbl::String> filter_names({"succeed.sh"});
    const signed char verbosity = -1;
    EXPECT_TRUE(
        RunTestsInDir(
            kMemFSPath, filter_names, nullptr, nullptr, verbosity, &num_failed, &results));
    EXPECT_EQ(0, num_failed);
    EXPECT_EQ(1, results.size());
    EXPECT_STR_EQ(results[0].name.c_str(), succeed_file.path().data());
    END_TEST;
}

bool RunTestsInDirWithVerbosity() {
    BEGIN_TEST;
    ScopedScriptFile succeed_file("/test-memfs/succeed.sh", kEchoSuccessAndArgs);
    int num_failed;
    fbl::Vector<Result> results;
    const signed char verbosity = 77;
    const char output_dir[] = "/test-memfs/output";
    ASSERT_EQ(0, MkDirAll(output_dir));
    EXPECT_TRUE(
        RunTestsInDir(
            kMemFSPath, {}, output_dir, kOutputFileName, verbosity, &num_failed, &results));
    EXPECT_EQ(0, num_failed);
    EXPECT_EQ(1, results.size());
    FILE* output_file = fopen(
        JoinPath(JoinPath(output_dir, succeed_file.path()), kOutputFileName).c_str(), "r");
    ASSERT_TRUE(output_file);
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    EXPECT_LT(0, fread(buf, sizeof(buf[0]), sizeof(buf), output_file));
    fclose(output_file);
    EXPECT_STR_EQ("Success! v=77\n", buf);
    END_TEST;
}

BEGIN_TEST_CASE(RunTest)
RUN_TEST(RunTestSuccess)
RUN_TEST(RunTestSuccessWithStdout)
RUN_TEST(RunTestFailureWithStderr)
RUN_TEST(RunTestFailureToLoadFile)
END_TEST_CASE(RunTest)

BEGIN_TEST_CASE(ParseTestNames)
RUN_TEST(ParseTestNamesEmptyStr)
RUN_TEST(ParseTestNamesEmptyStrInMiddle)
RUN_TEST(ParseTestNamesNormal)
RUN_TEST(ParseTestNamesTrailingComma)
END_TEST_CASE(ParseTestNames)

BEGIN_TEST_CASE(IsInWhitelist)
RUN_TEST(EmptyWhitelist)
RUN_TEST(NonemptyWhitelist)
END_TEST_CASE(IsInWhitelist)

BEGIN_TEST_CASE(JoinPath)
RUN_TEST(JoinPathNoTrailingSlash)
RUN_TEST(JoinPathTrailingSlash)
RUN_TEST(JoinPathAbsoluteChild)
END_TEST_CASE(JoinPath)

BEGIN_TEST_CASE(MkDirAll)
RUN_TEST(MkDirAllTooLong)
RUN_TEST(MkDirAllAlreadyExists)
RUN_TEST(MkDirAllParentAlreadyExists)
RUN_TEST(MkDirAllParentDoesNotExist)
END_TEST_CASE(MkDirAll)

BEGIN_TEST_CASE(WriteSummaryJSON)
RUN_TEST(WriteSummaryJSONSucceeds)
RUN_TEST(WriteSummaryJSONBadTestName)
END_TEST_CASE(WriteSummaryJSON)

BEGIN_TEST_CASE(ResolveGlobs)
RUN_TEST(ResolveGlobsNoMatches)
RUN_TEST(ResolveGlobsMultipleMatches)
END_TEST_CASE(ResolveGlobs)

BEGIN_TEST_CASE(RunTestsInDir)
RUN_TEST(RunTestsInDirBasic)
RUN_TEST(RunTestsInDirFilter)
RUN_TEST(RunTestsInDirWithVerbosity)
END_TEST_CASE(RunTestsInDir)

} // namespace
} // namespace runtests

int main(int argc, char** argv) {
    // Initialize memfs, for use in tests that touch the file system.
    async::Loop loop;
    if (loop.StartThread() != ZX_OK) {
        fprintf(stderr, "Error: Cannot initialize local memfs loop\n");
        return -1;
    }
    if (memfs_install_at(loop.async(), runtests::kMemFSPath) != ZX_OK) {
        fprintf(stderr, "Error: Cannot install local memfs\n");
        return -1;
    }

    return unittest_run_all_tests(argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE;
}
