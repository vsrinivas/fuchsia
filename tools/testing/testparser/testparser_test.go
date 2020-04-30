// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"bytes"
	"encoding/json"
	"testing"
)

func compactJson(jsonBytes []byte) []byte {
	buffer := bytes.NewBuffer([]byte{})
	json.Compact(buffer, jsonBytes)
	return buffer.Bytes()
}

func indentJson(jsonBytes []byte) []byte {
	buffer := bytes.NewBuffer([]byte{})
	json.Indent(buffer, jsonBytes, "", "\t")
	return buffer.Bytes()
}

func testCase(t *testing.T, stdout string, want string) {
	t.Helper()
	actual, _ := json.Marshal(Parse([]byte(stdout)))
	if !bytes.Equal(actual, compactJson([]byte(want))) {
		actualIndented := string(indentJson(actual))
		wantIndented := string(indentJson([]byte(want)))
		t.Errorf("Parse(stdout) = `\n%v\n`; want `\n%v\n``", actualIndented, wantIndented)
	}
}

func TestParseEmpty(t *testing.T) {
	testCase(t, "", "[]")
}

func TestParseInvalid(t *testing.T) {
	stdout := `
Mary had a little lamb
Its fleece was white as snow
And everywhere that Mary went
The lamb was sure to go
`
	testCase(t, stdout, "[]")
}

func TestParseGoogleTest(t *testing.T) {
	stdout := `
Some times there is weird stuff in stdout.
[==========] Running 9 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 9 tests from SynonymDictTest
[ RUN      ] SynonymDictTest.IsInitializedEmpty
[       OK ] SynonymDictTest.IsInitializedEmpty (4 ms)
[ RUN      ] SynonymDictTest.ReadingEmptyFileReturnsFalse
[       OK ] SynonymDictTest.ReadingEmptyFileReturnsFalse (3 ms)
[ RUN      ] SynonymDictTest.ReadingNonexistentFileReturnsFalse
Some times tests print to stdout.
Their prints get interleaved with the results.
[       OK ] SynonymDictTest.ReadingNonexistentFileReturnsFalse (4 ms)
[ RUN      ] SynonymDictTest.LoadDictionary
[       OK ] SynonymDictTest.LoadDictionary (4 ms)
[ RUN      ] SynonymDictTest.GetSynonymsReturnsListOfWords
[       OK ] SynonymDictTest.GetSynonymsReturnsListOfWords (4 ms)
[ RUN      ] SynonymDictTest.GetSynonymsWhenNoSynonymsAreAvailable
[       OK ] SynonymDictTest.GetSynonymsWhenNoSynonymsAreAvailable (4 ms)
[ RUN      ] SynonymDictTest.AllWordsAreSynonymsOfEachOther
[       OK ] SynonymDictTest.AllWordsAreSynonymsOfEachOther (4 ms)
[ RUN      ] SynonymDictTest.GetSynonymsReturnsListOfWordsWithStubs
[  FAILED  ] SynonymDictTest.GetSynonymsReturnsListOfWordsWithStubs (4 ms)
[ RUN      ] SynonymDictTest.CompoundWordBug
[  SKIPPED ] SynonymDictTest.CompoundWordBug (4 ms)
[----------] 9 tests from SynonymDictTest (36 ms total)
[----------] Global test environment tear-down
[==========] 9 tests from 1 test suite ran. (38 ms total)
[  PASSED  ] 9 tests.
`
	want := `
[
	{
		"display_name": "SynonymDictTest.IsInitializedEmpty",
		"suite_name": "SynonymDictTest",
		"case_name": "IsInitializedEmpty",
		"status": "Pass",
		"duration_nanos": 4000000,
		"format": "GoogleTest"
	},
	{
		"display_name": "SynonymDictTest.ReadingEmptyFileReturnsFalse",
		"suite_name": "SynonymDictTest",
		"case_name": "ReadingEmptyFileReturnsFalse",
		"status": "Pass",
		"duration_nanos": 3000000,
		"format": "GoogleTest"
	},
	{
		"display_name": "SynonymDictTest.ReadingNonexistentFileReturnsFalse",
		"suite_name": "SynonymDictTest",
		"case_name": "ReadingNonexistentFileReturnsFalse",
		"status": "Pass",
		"duration_nanos": 4000000,
		"format": "GoogleTest"
	},
	{
		"display_name": "SynonymDictTest.LoadDictionary",
		"suite_name": "SynonymDictTest",
		"case_name": "LoadDictionary",
		"status": "Pass",
		"duration_nanos": 4000000,
		"format": "GoogleTest"
	},
	{
		"display_name": "SynonymDictTest.GetSynonymsReturnsListOfWords",
		"suite_name": "SynonymDictTest",
		"case_name": "GetSynonymsReturnsListOfWords",
		"status": "Pass",
		"duration_nanos": 4000000,
		"format": "GoogleTest"
	},
	{
		"display_name": "SynonymDictTest.GetSynonymsWhenNoSynonymsAreAvailable",
		"suite_name": "SynonymDictTest",
		"case_name": "GetSynonymsWhenNoSynonymsAreAvailable",
		"status": "Pass",
		"duration_nanos": 4000000,
		"format": "GoogleTest"
	},
	{
		"display_name": "SynonymDictTest.AllWordsAreSynonymsOfEachOther",
		"suite_name": "SynonymDictTest",
		"case_name": "AllWordsAreSynonymsOfEachOther",
		"status": "Pass",
		"duration_nanos": 4000000,
		"format": "GoogleTest"
	},
	{
		"display_name": "SynonymDictTest.GetSynonymsReturnsListOfWordsWithStubs",
		"suite_name": "SynonymDictTest",
		"case_name": "GetSynonymsReturnsListOfWordsWithStubs",
		"status": "Fail",
		"duration_nanos": 4000000,
		"format": "GoogleTest"
	},
	{
		"display_name": "SynonymDictTest.CompoundWordBug",
		"suite_name": "SynonymDictTest",
		"case_name": "CompoundWordBug",
		"status": "Skip",
		"duration_nanos": 4000000,
		"format": "GoogleTest"
	}
]
`
	testCase(t, stdout, want)
}

func TestParseGo(t *testing.T) {
	stdout := `
==================== Test output for //experimental/users/shayba/testparser:test:
=== RUN   TestParseEmpty
--- PASS: TestParseEmpty (0.01s)
=== RUN   TestParseInvalid
--- PASS: TestParseInvalid (0.02s)
=== RUN   TestParseGoogleTest
    TestParseGoogleTest: experimental/users/shayba/testparser/testparser_test.go:15: Parse(invalid).Parse() = [{SynonymDictTest.IsInitializedEmpty Pass 4} {SynonymDictTest.ReadingEmptyFileReturnsFalse Pass 3} {SynonymDictTest.ReadingNonexistentFileReturnsFalse Pass 4} {SynonymDictTest.LoadDictionary Pass 4} {SynonymDictTest.GetSynonymsReturnsListOfWords Pass 4} {SynonymDictTest.GetSynonymsWhenNoSynonymsAreAvailable Pass 4} {SynonymDictTest.AllWordsAreSynonymsOfEachOther Pass 4} {SynonymDictTest.GetSynonymsReturnsListOfWordsWithStubs Fail 4} {SynonymDictTest.CompoundWordBug Skip 4}]; want []
--- FAIL: TestParseGoogleTest (3.00s)
=== RUN   TestFail
    TestFail: experimental/users/shayba/testparser/testparser_test.go:68: Oops!
--- FAIL: TestFail (0.00s)
=== RUN   TestSkip
    TestSkip: experimental/users/shayba/testparser/testparser_test.go:72: Huh?
--- SKIP: TestSkip (0.00s)
FAIL
`
	want := `
[
	{
		"display_name": "//experimental/users/shayba/testparser.TestParseEmpty",
		"suite_name": "//experimental/users/shayba/testparser",
		"case_name": "TestParseEmpty",
		"status": "Pass",
		"duration_nanos": 10000000,
		"format": "Go"
	},
	{
		"display_name": "//experimental/users/shayba/testparser.TestParseInvalid",
		"suite_name": "//experimental/users/shayba/testparser",
		"case_name": "TestParseInvalid",
		"status": "Pass",
		"duration_nanos": 20000000,
		"format": "Go"
	},
	{
		"display_name": "//experimental/users/shayba/testparser.TestParseGoogleTest",
		"suite_name": "//experimental/users/shayba/testparser",
		"case_name": "TestParseGoogleTest",
		"status": "Fail",
		"duration_nanos": 3000000000,
		"format": "Go"
	},
	{
		"display_name": "//experimental/users/shayba/testparser.TestFail",
		"suite_name": "//experimental/users/shayba/testparser",
		"case_name": "TestFail",
		"status": "Fail",
		"duration_nanos": 0,
		"format": "Go"
	},
	{
		"display_name": "//experimental/users/shayba/testparser.TestSkip",
		"suite_name": "//experimental/users/shayba/testparser",
		"case_name": "TestSkip",
		"status": "Skip",
		"duration_nanos": 0,
		"format": "Go"
	}
]
`
	testCase(t, stdout, want)
}

func TestParseRust(t *testing.T) {
	stdout := `
running 3 tests
test tests::ignored_test ... ignored
test tests::test_add_hundred ... ok
test tests::test_add ... FAILED

failures:

---- tests::test_add stdout ----
thread 'tests::test_add' panicked at 'assertion failed: ` + "`(left == right)`" + `
  left: ` + "`1`" + `,
 right: ` + "`2`" + `', src/lib.rs:12:9
note: run with ` + "`RUST_BACKTRACE=1`" + ` environment variable to display a backtrace


failures:
    tests::test_add

test result: FAILED. 1 passed; 1 failed; 1 ignored; 0 measured; 0 filtered out
`
	want := `
[
	{
		"display_name": "tests::ignored_test",
		"suite_name": "tests",
		"case_name": "ignored_test",
		"status": "Skip",
		"duration_nanos": 0,
		"format": "Rust"
	},
	{
		"display_name": "tests::test_add_hundred",
		"suite_name": "tests",
		"case_name": "test_add_hundred",
		"status": "Pass",
		"duration_nanos": 0,
		"format": "Rust"
	},
	{
		"display_name": "tests::test_add",
		"suite_name": "tests",
		"case_name": "test_add",
		"status": "Fail",
		"duration_nanos": 0,
		"format": "Rust"
	}
]
`
	testCase(t, stdout, want)
}

func TestParseZircon(t *testing.T) {
	stdout := `
CASE minfs_truncate_tests                               [STARTED] 
    TestTruncateSmall                                   [RUNNING] [PASSED] (1 ms)
    (TestTruncateLarge<1 << 10, 1000>)                  [RUNNING] [PASSED] (20414 ms)
    (TestTruncateLarge<1 << 15, 500>)                   [RUNNING] [PASSED] (10012 ms)
    (TestTruncateLarge<1 << 20, 500>)                   [RUNNING] [PASSED] (10973 ms)
    (TestTruncateLarge<1 << 25, 500>)                   [IGNORED]
CASE minfs_truncate_tests                               [PASSED]

CASE minfs_sparse_tests                                 [STARTED] 
    (test_sparse<0, 0, kBlockSize>)                     [RUNNING] [PASSED] (19 ms)
    (test_sparse<kBlockSize / 2, 0, kBlockSize>)        [RUNNING] [PASSED] (20 ms)
    (test_sparse<kBlockSize / 2, kBlockSize, kBlockSize>) [RUNNING] [PASSED] (19 ms)
    (test_sparse<kBlockSize, 0, kBlockSize>)            [RUNNING] [PASSED] (19 ms)
    (test_sparse<kBlockSize, kBlockSize / 2, kBlockSize>) [RUNNING] [PASSED] (19 ms)
    (test_sparse<kBlockSize * kDirectBlocks, kBlockSize * kDirectBlocks - kBlockSize, kBlockSize * 2>) [RUNNING] [PASSED] (20 ms)
    (test_sparse<kBlockSize * kDirectBlocks, kBlockSize * kDirectBlocks - kBlockSize, kBlockSize * 32>) [RUNNING] [PASSED] (24 ms)
    (test_sparse<kBlockSize * kDirectBlocks + kBlockSize, kBlockSize * kDirectBlocks - kBlockSize, kBlockSize * 32>) [RUNNING] [PASSED] (24 ms)
    (test_sparse<kBlockSize * kDirectBlocks + kBlockSize, kBlockSize * kDirectBlocks + 2 * kBlockSize, kBlockSize * 32>) [RUNNING] [PASSED] (25 ms)
CASE minfs_sparse_tests                                 [PASSED]

CASE minfs_rw_workers_test                              [STARTED] 
    TestWorkSingleThread                                [RUNNING] [PASSED] (40920 ms)
CASE minfs_rw_workers_test                              [PASSED]

CASE minfs_maxfile_tests                                [STARTED] 
    test_maxfile                                        [RUNNING] [PASSED] (62243 ms)
CASE minfs_maxfile_tests                                [PASSED]

CASE minfs_directory_tests                              [STARTED] 
    TestDirectoryLarge                                  [RUNNING] [PASSED] (3251 ms)
    TestDirectoryReaddir                                [RUNNING] [PASSED] (69 ms)
    TestDirectoryReaddirLarge                           [RUNNING] [PASSED] (6414 ms)
CASE minfs_directory_tests                              [PASSED]

CASE minfs_basic_tests                                  [STARTED] 
    test_basic                                          [RUNNING] [PASSED] (21 ms)
CASE minfs_basic_tests                                  [PASSED]
====================================================
Results for test binary "host_x64-asan/fs-host":
    SUCCESS!  All test cases passed!
    CASES:  6     SUCCESS:  6     FAILED:  0   
====================================================
`
	want := `
[
	{
		"display_name": "minfs_truncate_tests.TestTruncateSmall",
		"suite_name": "minfs_truncate_tests",
		"case_name": "TestTruncateSmall",
		"status": "Pass",
		"duration_nanos": 1000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_truncate_tests.(TestTruncateLarge\u003c1 \u003c\u003c 10, 1000\u003e)",
		"suite_name": "minfs_truncate_tests",
		"case_name": "(TestTruncateLarge\u003c1 \u003c\u003c 10, 1000\u003e)",
		"status": "Pass",
		"duration_nanos": 20414000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_truncate_tests.(TestTruncateLarge\u003c1 \u003c\u003c 15, 500\u003e)",
		"suite_name": "minfs_truncate_tests",
		"case_name": "(TestTruncateLarge\u003c1 \u003c\u003c 15, 500\u003e)",
		"status": "Pass",
		"duration_nanos": 10012000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_truncate_tests.(TestTruncateLarge\u003c1 \u003c\u003c 20, 500\u003e)",
		"suite_name": "minfs_truncate_tests",
		"case_name": "(TestTruncateLarge\u003c1 \u003c\u003c 20, 500\u003e)",
		"status": "Pass",
		"duration_nanos": 10973000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_truncate_tests.(TestTruncateLarge\u003c1 \u003c\u003c 25, 500\u003e)",
		"suite_name": "minfs_truncate_tests",
		"case_name": "(TestTruncateLarge\u003c1 \u003c\u003c 25, 500\u003e)",
		"status": "Skip",
		"duration_nanos": 0,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_sparse_tests.(test_sparse\u003c0, 0, kBlockSize\u003e)",
		"suite_name": "minfs_sparse_tests",
		"case_name": "(test_sparse\u003c0, 0, kBlockSize\u003e)",
		"status": "Pass",
		"duration_nanos": 19000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_sparse_tests.(test_sparse\u003ckBlockSize / 2, 0, kBlockSize\u003e)",
		"suite_name": "minfs_sparse_tests",
		"case_name": "(test_sparse\u003ckBlockSize / 2, 0, kBlockSize\u003e)",
		"status": "Pass",
		"duration_nanos": 20000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_sparse_tests.(test_sparse\u003ckBlockSize / 2, kBlockSize, kBlockSize\u003e)",
		"suite_name": "minfs_sparse_tests",
		"case_name": "(test_sparse\u003ckBlockSize / 2, kBlockSize, kBlockSize\u003e)",
		"status": "Pass",
		"duration_nanos": 19000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_sparse_tests.(test_sparse\u003ckBlockSize, 0, kBlockSize\u003e)",
		"suite_name": "minfs_sparse_tests",
		"case_name": "(test_sparse\u003ckBlockSize, 0, kBlockSize\u003e)",
		"status": "Pass",
		"duration_nanos": 19000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_sparse_tests.(test_sparse\u003ckBlockSize, kBlockSize / 2, kBlockSize\u003e)",
		"suite_name": "minfs_sparse_tests",
		"case_name": "(test_sparse\u003ckBlockSize, kBlockSize / 2, kBlockSize\u003e)",
		"status": "Pass",
		"duration_nanos": 19000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_sparse_tests.(test_sparse\u003ckBlockSize * kDirectBlocks, kBlockSize * kDirectBlocks - kBlockSize, kBlockSize * 2\u003e)",
		"suite_name": "minfs_sparse_tests",
		"case_name": "(test_sparse\u003ckBlockSize * kDirectBlocks, kBlockSize * kDirectBlocks - kBlockSize, kBlockSize * 2\u003e)",
		"status": "Pass",
		"duration_nanos": 20000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_sparse_tests.(test_sparse\u003ckBlockSize * kDirectBlocks, kBlockSize * kDirectBlocks - kBlockSize, kBlockSize * 32\u003e)",
		"suite_name": "minfs_sparse_tests",
		"case_name": "(test_sparse\u003ckBlockSize * kDirectBlocks, kBlockSize * kDirectBlocks - kBlockSize, kBlockSize * 32\u003e)",
		"status": "Pass",
		"duration_nanos": 24000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_sparse_tests.(test_sparse\u003ckBlockSize * kDirectBlocks + kBlockSize, kBlockSize * kDirectBlocks - kBlockSize, kBlockSize * 32\u003e)",
		"suite_name": "minfs_sparse_tests",
		"case_name": "(test_sparse\u003ckBlockSize * kDirectBlocks + kBlockSize, kBlockSize * kDirectBlocks - kBlockSize, kBlockSize * 32\u003e)",
		"status": "Pass",
		"duration_nanos": 24000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_sparse_tests.(test_sparse\u003ckBlockSize * kDirectBlocks + kBlockSize, kBlockSize * kDirectBlocks + 2 * kBlockSize, kBlockSize * 32\u003e)",
		"suite_name": "minfs_sparse_tests",
		"case_name": "(test_sparse\u003ckBlockSize * kDirectBlocks + kBlockSize, kBlockSize * kDirectBlocks + 2 * kBlockSize, kBlockSize * 32\u003e)",
		"status": "Pass",
		"duration_nanos": 25000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_rw_workers_test.TestWorkSingleThread",
		"suite_name": "minfs_rw_workers_test",
		"case_name": "TestWorkSingleThread",
		"status": "Pass",
		"duration_nanos": 40920000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_maxfile_tests.test_maxfile",
		"suite_name": "minfs_maxfile_tests",
		"case_name": "test_maxfile",
		"status": "Pass",
		"duration_nanos": 62243000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_directory_tests.TestDirectoryLarge",
		"suite_name": "minfs_directory_tests",
		"case_name": "TestDirectoryLarge",
		"status": "Pass",
		"duration_nanos": 3251000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_directory_tests.TestDirectoryReaddir",
		"suite_name": "minfs_directory_tests",
		"case_name": "TestDirectoryReaddir",
		"status": "Pass",
		"duration_nanos": 69000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_directory_tests.TestDirectoryReaddirLarge",
		"suite_name": "minfs_directory_tests",
		"case_name": "TestDirectoryReaddirLarge",
		"status": "Pass",
		"duration_nanos": 6414000000,
		"format": "Zircon utest"
	},
	{
		"display_name": "minfs_basic_tests.test_basic",
		"suite_name": "minfs_basic_tests",
		"case_name": "test_basic",
		"status": "Pass",
		"duration_nanos": 21000000,
		"format": "Zircon utest"
	}
]
`
	testCase(t, stdout, want)
}
