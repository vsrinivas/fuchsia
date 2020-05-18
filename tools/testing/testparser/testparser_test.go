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

func TestParseFtfTest(t *testing.T) {
	stdout := `
Running test 'fuchsia-pkg://fuchsia.com/twoplustwo-rust-unittest#meta/twoplustwo-rust-unittest.cm'
[RUNNING]	tests::two_plus_two
[PASSED]	tests::two_plus_two
1 out of 1 tests passed...
fuchsia-pkg://fuchsia.com/twoplustwo-rust-unittest#meta/twoplustwo-rust-unittest.cm completed with result: PASSED
ok 23 fuchsia-pkg://fuchsia.com/twoplustwo-rust-unittest#meta/twoplustwo-rust-unittest.cm (1.05014413s)`
	want := `
        [
                {
                        "display_name": "tests::two_plus_two",
                        "suite_name": "",
                        "case_name": "tests::two_plus_two",
                        "status": "Pass",
                        "duration_nanos": 0,
                        "format": "FTF"
                }
        ]
`
	testCase(t, stdout, want)
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
		"display_name": "//experimental/users/shayba/testparser:test.TestParseEmpty",
		"suite_name": "//experimental/users/shayba/testparser:test",
		"case_name": "TestParseEmpty",
		"status": "Pass",
		"duration_nanos": 10000000,
		"format": "Go"
	},
	{
		"display_name": "//experimental/users/shayba/testparser:test.TestParseInvalid",
		"suite_name": "//experimental/users/shayba/testparser:test",
		"case_name": "TestParseInvalid",
		"status": "Pass",
		"duration_nanos": 20000000,
		"format": "Go"
	},
	{
		"display_name": "//experimental/users/shayba/testparser:test.TestParseGoogleTest",
		"suite_name": "//experimental/users/shayba/testparser:test",
		"case_name": "TestParseGoogleTest",
		"status": "Fail",
		"duration_nanos": 3000000000,
		"format": "Go"
	},
	{
		"display_name": "//experimental/users/shayba/testparser:test.TestFail",
		"suite_name": "//experimental/users/shayba/testparser:test",
		"case_name": "TestFail",
		"status": "Fail",
		"duration_nanos": 0,
		"format": "Go"
	},
	{
		"display_name": "//experimental/users/shayba/testparser:test.TestSkip",
		"suite_name": "//experimental/users/shayba/testparser:test",
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
    (TestTruncateLarge<1 << 20, 500>)                   [RUNNING] [FAILED] (10973 ms)
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
    CASES:  6     SUCCESS:  5     FAILED:  1   
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
		"status": "Fail",
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

// Regression test for fxb/51327
func TestFxb51327(t *testing.T) {
	stdout := `
[==========] Running 13 tests from 3 test suites.
[----------] Global test environment set-up.
[----------] 10 tests from UltrasoundTest
[ RUN      ] UltrasoundTest.CreateRenderer
[00392.026593][276880][276883][test-devmgr] INFO: 
[00392.026655][276880][276883][test-devmgr] INFO: Running remove task for device 0xc64fcfe910 'Virtual_Audio_Device_(default)'
[00392.026783][276880][276883][test-devmgr] INFO: Removed device 0xc64fcfe910 'Virtual_Audio_Device_(default)': ZX_OK
[00392.026793][276880][276883][test-devmgr] INFO: Removing device 0xc64fcfe910 'Virtual_Audio_Device_(default)' parent=0xc64fcfe610
[00391.063831][276429][279657][audio_pipeline_test] INFO: [hermetic_audio_environment.cc(40)] Using path '/pkg/data/ultrasound' for /config/data directory for fuchsia-pkg://fuchsia.com/audio_core#meta/audio_core_nodevfs_noconfigdata.cmx.
[00391.064291][276429][279657][audio_pipeline_test] INFO: [hermetic_audio_environment.cc(54)] No config_data provided for fuchsia-pkg://fuchsia.com/virtual_audio_service#meta/virtual_audio_service_nodevfs.cmx
[00391.820503][280027][280029][audio_core] INFO: [main.cc(36)] AudioCore starting up
[00391.891903][280027][280029][audio_core] INFO: [policy_loader.cc(244)] No policy found; using default.
[00392.007930][280027][280375][audio_core] ERROR: [src/media/audio/audio_core/driver_output.cc(166)] OUTPUT UNDERFLOW: Missed mix target by (worst-case, expected) = (49, 99) ms. Cooling down for 1000 milliseconds.
[00392.007972][280027][280375][audio_core] ERROR: [src/media/audio/audio_core/reporter.cc(428)] UNDERFLOW: Failed to obtain the Cobalt logger
[00392.026545][280027][280375][audio_core] INFO: [audio_driver_v2.cc(585)] Output shutting down 'Stream channel closed unexpectedly', status:-24
[00392.026600][280027][280375][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(75)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[       OK ] UltrasoundTest.CreateRenderer (964 ms)
[ RUN      ] UltrasoundTest.RendererDoesNotSupportSetPcmStreamType
[00392.169044][276880][276883][test-devmgr] INFO: 
[00392.169124][276880][276883][test-devmgr] INFO: Running remove task for device 0xc64fcfe910 'Virtual_Audio_Device_(default)'
[00392.169299][276880][276883][test-devmgr] INFO: Removed device 0xc64fcfe910 'Virtual_Audio_Device_(default)': ZX_OK
[00392.169321][276880][276883][test-devmgr] INFO: Removing device 0xc64fcfe910 'Virtual_Audio_Device_(default)' parent=0xc64fcfe610
[00392.026628][280027][280375][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(305)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[00392.140514][280027][280436][audio_core] ERROR: [src/media/audio/audio_core/driver_output.cc(166)] OUTPUT UNDERFLOW: Missed mix target by (worst-case, expected) = (48.95, 98) ms. Cooling down for 1000 milliseconds.
[00392.140549][280027][280436][audio_core] ERROR: [src/media/audio/audio_core/reporter.cc(428)] UNDERFLOW: Failed to obtain the Cobalt logger
[00392.158798][280027][280029][audio_core] ERROR: [src/media/audio/audio_core/ultrasound_renderer.cc(55)] Unsupported method SetPcmStreamType on ultrasound renderer
[00392.168963][280027][280436][audio_core] INFO: [audio_driver_v2.cc(585)] Output shutting down 'Stream channel closed unexpectedly', status:-24
[       OK ] UltrasoundTest.RendererDoesNotSupportSetPcmStreamType (142 ms)
[ RUN      ] UltrasoundTest.RendererDoesNotSupportSetUsage
[00392.281231][276880][276883][test-devmgr] INFO: 
[00392.281289][276880][276883][test-devmgr] INFO: Running remove task for device 0xc64fcfe910 'Virtual_Audio_Device_(default)'
[00392.281417][276880][276883][test-devmgr] INFO: Removed device 0xc64fcfe910 'Virtual_Audio_Device_(default)': ZX_OK
[00392.281428][276880][276883][test-devmgr] INFO: Removing device 0xc64fcfe910 'Virtual_Audio_Device_(default)' parent=0xc64fcfe610
[00392.169001][280027][280436][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(75)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[00392.169021][280027][280436][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(305)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[00392.253474][280027][280498][audio_core] ERROR: [src/media/audio/audio_core/driver_output.cc(166)] OUTPUT UNDERFLOW: Missed mix target by (worst-case, expected) = (20.51, 70) ms. Cooling down for 1000 milliseconds.
[00392.253501][280027][280498][audio_core] ERROR: [src/media/audio/audio_core/reporter.cc(428)] UNDERFLOW: Failed to obtain the Cobalt logger
[00392.270892][280027][280029][audio_core] ERROR: [src/media/audio/audio_core/ultrasound_renderer.cc(59)] Unsupported method SetUsage on ultrasound renderer
[00392.281120][280027][280498][audio_core] INFO: [audio_driver_v2.cc(585)] Output shutting down 'Stream channel closed unexpectedly', status:-24
[       OK ] UltrasoundTest.RendererDoesNotSupportSetUsage (112 ms)
[ RUN      ] UltrasoundTest.RendererDoesNotSupportBindGainControl
[00392.423415][276880][276883][test-devmgr] INFO: 
[00392.423473][276880][276883][test-devmgr] INFO: Running remove task for device 0xc64fcfe910 'Virtual_Audio_Device_(default)'
[00392.423785][276880][276883][test-devmgr] INFO: Removed device 0xc64fcfe910 'Virtual_Audio_Device_(default)': ZX_OK
[00392.423800][276880][276883][test-devmgr] INFO: Removing device 0xc64fcfe910 'Virtual_Audio_Device_(default)' parent=0xc64fcfe610
[00392.281148][280027][280498][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(75)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[00392.281169][280027][280498][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(305)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[00392.394476][280027][280562][audio_core] ERROR: [src/media/audio/audio_core/driver_output.cc(166)] OUTPUT UNDERFLOW: Missed mix target by (worst-case, expected) = (49, 99) ms. Cooling down for 1000 milliseconds.
[00392.394513][280027][280562][audio_core] ERROR: [src/media/audio/audio_core/reporter.cc(428)] UNDERFLOW: Failed to obtain the Cobalt logger
[00392.413146][280027][280029][audio_core] ERROR: [src/media/audio/audio_core/ultrasound_renderer.cc(64)] Unsupported method BindGainControl on ultrasound renderer
[00392.423325][280027][280562][audio_core] INFO: [audio_driver_v2.cc(585)] Output shutting down 'Stream channel closed unexpectedly', status:-24
[00392.423387][280027][280562][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(75)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[00392.423417][280027][280562][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(305)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[       OK ] UltrasoundTest.RendererDoesNotSupportBindGainControl (151 ms)
[ RUN      ] UltrasoundTest.RendererDoesNotSupportSetReferenceClock
[00392.575423][276880][276883][test-devmgr] INFO: 
[00392.575484][276880][276883][test-devmgr] INFO: Running remove task for device 0xc64fcfe910 'Virtual_Audio_Device_(default)'
[00392.575618][276880][276883][test-devmgr] INFO: Removed device 0xc64fcfe910 'Virtual_Audio_Device_(default)': ZX_OK
[00392.575627][276880][276883][test-devmgr] INFO: Removing device 0xc64fcfe910 'Virtual_Audio_Device_(default)' parent=0xc64fcfe610
[00392.546406][280027][280625][audio_core] ERROR: [src/media/audio/audio_core/driver_output.cc(166)] OUTPUT UNDERFLOW: Missed mix target by (worst-case, expected) = (48.99, 98) ms. Cooling down for 1000 milliseconds.
[00392.546445][280027][280625][audio_core] ERROR: [src/media/audio/audio_core/reporter.cc(428)] UNDERFLOW: Failed to obtain the Cobalt logger
[00392.565178][280027][280029][audio_core] ERROR: [src/media/audio/audio_core/ultrasound_renderer.cc(68)] Unsupported method SetReferenceClock on ultrasound renderer
[00392.575361][280027][280625][audio_core] INFO: [audio_driver_v2.cc(585)] Output shutting down 'Stream channel closed unexpectedly', status:-24
[       OK ] UltrasoundTest.RendererDoesNotSupportSetReferenceClock (143 ms)
[ RUN      ] UltrasoundTest.CreateCapturer
[00392.607433][276880][276883][test-devmgr] INFO: 
[00392.607488][276880][276883][test-devmgr] INFO: Running remove task for device 0xc64fcfe910 'Virtual_Audio_Device_(default)'
[00392.607609][276880][276883][test-devmgr] INFO: Removed device 0xc64fcfe910 'Virtual_Audio_Device_(default)': ZX_OK
[00392.607620][276880][276883][test-devmgr] INFO: Removing device 0xc64fcfe910 'Virtual_Audio_Device_(default)' parent=0xc64fcfe610
[00392.575406][280027][280625][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(75)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[00392.575427][280027][280625][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(305)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[00392.607377][280027][280686][audio_core] INFO: [audio_driver_v2.cc(585)]  Input shutting down 'Stream channel closed unexpectedly', status:-24
[       OK ] UltrasoundTest.CreateCapturer (32 ms)
[ RUN      ] UltrasoundTest.CapturerDoesNotSupportSetPcmStreamType
[00392.649557][276880][276883][test-devmgr] INFO: 
[00392.649636][276880][276883][test-devmgr] INFO: Running remove task for device 0xc64fcfe910 'Virtual_Audio_Device_(default)'
[00392.649796][276880][276883][test-devmgr] INFO: Removed device 0xc64fcfe910 'Virtual_Audio_Device_(default)': ZX_OK
[00392.649812][276880][276883][test-devmgr] INFO: Removing device 0xc64fcfe910 'Virtual_Audio_Device_(default)' parent=0xc64fcfe610
[00392.607415][280027][280686][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(75)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[00392.607436][280027][280686][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(305)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[00392.639303][280027][280029][audio_core] ERROR: [src/media/audio/audio_core/ultrasound_capturer.cc(61)] Unsupported method SetPcmStreamType on ultrasound capturer
[00392.649486][280027][280755][audio_core] INFO: [audio_driver_v2.cc(585)]  Input shutting down 'Stream channel closed unexpectedly', status:-24
[00392.649540][280027][280755][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(75)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[00392.649569][280027][280755][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(305)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[       OK ] UltrasoundTest.CapturerDoesNotSupportSetPcmStreamType (51 ms)
[ RUN      ] UltrasoundTest.CapturerDoesNotSupportSetUsage
[00392.701181][276880][276883][test-devmgr] INFO: 
[00392.701258][276880][276883][test-devmgr] INFO: Running remove task for device 0xc64fcfe910 'Virtual_Audio_Device_(default)'
[00392.701417][276880][276883][test-devmgr] INFO: Removed device 0xc64fcfe910 'Virtual_Audio_Device_(default)': ZX_OK
[00392.701431][276880][276883][test-devmgr] INFO: Removing device 0xc64fcfe910 'Virtual_Audio_Device_(default)' parent=0xc64fcfe610
[00392.690860][280027][280029][audio_core] ERROR: [src/media/audio/audio_core/ultrasound_capturer.cc(57)] Unsupported method SetUsage on ultrasound capturer
[00392.701096][280027][280824][audio_core] INFO: [audio_driver_v2.cc(585)]  Input shutting down 'Stream channel closed unexpectedly', status:-24
[00392.701154][280027][280824][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(75)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[00392.701185][280027][280824][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(305)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[       OK ] UltrasoundTest.CapturerDoesNotSupportSetUsage (42 ms)
[ RUN      ] UltrasoundTest.CapturerDoesNotSupportBindGainControl
[00392.743724][276880][276883][test-devmgr] INFO: 
[00392.743803][276880][276883][test-devmgr] INFO: Running remove task for device 0xc64fcfe910 'Virtual_Audio_Device_(default)'
[00392.743971][276880][276883][test-devmgr] INFO: Removed device 0xc64fcfe910 'Virtual_Audio_Device_(default)': ZX_OK
[00392.743989][276880][276883][test-devmgr] INFO: Removing device 0xc64fcfe910 'Virtual_Audio_Device_(default)' parent=0xc64fcfe610
[00392.733408][280027][280029][audio_core] ERROR: [src/media/audio/audio_core/ultrasound_capturer.cc(66)] Unsupported method BindGainControl on ultrasound capturer
[00392.743640][280027][280891][audio_core] INFO: [audio_driver_v2.cc(585)]  Input shutting down 'Stream channel closed unexpectedly', status:-24
[00392.743708][280027][280891][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(75)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[00392.743738][280027][280891][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(305)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[       OK ] UltrasoundTest.CapturerDoesNotSupportBindGainControl (52 ms)
[ RUN      ] UltrasoundTest.CapturerDoesNotSupportSetReferenceClock
[00392.795276][276880][276883][test-devmgr] INFO: 
[00392.795333][276880][276883][test-devmgr] INFO: Running remove task for device 0xc64fcfe910 'Virtual_Audio_Device_(default)'
[00392.795496][276880][276883][test-devmgr] INFO: Removed device 0xc64fcfe910 'Virtual_Audio_Device_(default)': ZX_OK
[00392.795530][276880][276883][test-devmgr] INFO: Removing device 0xc64fcfe910 'Virtual_Audio_Device_(default)' parent=0xc64fcfe610
[00392.784991][280027][280029][audio_core] ERROR: [src/media/audio/audio_core/ultrasound_capturer.cc(70)] Unsupported method SetReferenceClock on ultrasound capturer
[00392.795201][280027][280962][audio_core] INFO: [audio_driver_v2.cc(585)]  Input shutting down 'Stream channel closed unexpectedly', status:-24
[       OK ] UltrasoundTest.CapturerDoesNotSupportSetReferenceClock (41 ms)
[00392.795237][280027][280962][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(75)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[00392.795258][280027][280962][audio_core] ERROR: [src/media/audio/audio_core/audio_driver_v2.cc(305)] AudioDriver failed with error: -24: -24 (ZX_ERR_PEER_CLOSED)
[----------] 10 tests from UltrasoundTest (1733 ms total)
[----------] Global test environment tear-down
[==========] 13 tests from 3 test suites ran. (10934 ms total)
[  PASSED  ] 13 tests.
ok 9 fuchsia-pkg://fuchsia.com/audio_pipeline_tests#meta/audio_pipeline_tests.cmx (12.230948553s)
`
	want := `
[
	{
		"display_name": "UltrasoundTest.CreateRenderer",
		"suite_name": "UltrasoundTest",
		"case_name": "CreateRenderer",
		"status": "Pass",
		"duration_nanos": 964000000,
		"format": "GoogleTest"
	},
	{
		"display_name": "UltrasoundTest.RendererDoesNotSupportSetPcmStreamType",
		"suite_name": "UltrasoundTest",
		"case_name": "RendererDoesNotSupportSetPcmStreamType",
		"status": "Pass",
		"duration_nanos": 142000000,
		"format": "GoogleTest"
	},
	{
		"display_name": "UltrasoundTest.RendererDoesNotSupportSetUsage",
		"suite_name": "UltrasoundTest",
		"case_name": "RendererDoesNotSupportSetUsage",
		"status": "Pass",
		"duration_nanos": 112000000,
		"format": "GoogleTest"
	},
	{
		"display_name": "UltrasoundTest.RendererDoesNotSupportBindGainControl",
		"suite_name": "UltrasoundTest",
		"case_name": "RendererDoesNotSupportBindGainControl",
		"status": "Pass",
		"duration_nanos": 151000000,
		"format": "GoogleTest"
	},
	{
		"display_name": "UltrasoundTest.RendererDoesNotSupportSetReferenceClock",
		"suite_name": "UltrasoundTest",
		"case_name": "RendererDoesNotSupportSetReferenceClock",
		"status": "Pass",
		"duration_nanos": 143000000,
		"format": "GoogleTest"
	},
	{
		"display_name": "UltrasoundTest.CreateCapturer",
		"suite_name": "UltrasoundTest",
		"case_name": "CreateCapturer",
		"status": "Pass",
		"duration_nanos": 32000000,
		"format": "GoogleTest"
	},
	{
		"display_name": "UltrasoundTest.CapturerDoesNotSupportSetPcmStreamType",
		"suite_name": "UltrasoundTest",
		"case_name": "CapturerDoesNotSupportSetPcmStreamType",
		"status": "Pass",
		"duration_nanos": 51000000,
		"format": "GoogleTest"
	},
	{
		"display_name": "UltrasoundTest.CapturerDoesNotSupportSetUsage",
		"suite_name": "UltrasoundTest",
		"case_name": "CapturerDoesNotSupportSetUsage",
		"status": "Pass",
		"duration_nanos": 42000000,
		"format": "GoogleTest"
	},
	{
		"display_name": "UltrasoundTest.CapturerDoesNotSupportBindGainControl",
		"suite_name": "UltrasoundTest",
		"case_name": "CapturerDoesNotSupportBindGainControl",
		"status": "Pass",
		"duration_nanos": 52000000,
		"format": "GoogleTest"
	},
	{
		"display_name": "UltrasoundTest.CapturerDoesNotSupportSetReferenceClock",
		"suite_name": "UltrasoundTest",
		"case_name": "CapturerDoesNotSupportSetReferenceClock",
		"status": "Pass",
		"duration_nanos": 41000000,
		"format": "GoogleTest"
	}
]
`
	testCase(t, stdout, want)
}

// Regression test for fxb/52363
func TestFxb52363(t *testing.T) {
	stdout := `
Running test in realm: test_env_25300c08
running 4 tests
test listen_for_klog ... ok
test listen_for_syslog ... ok
test listen_for_klog_routed_stdio ... ok
test test_observer_stop_api ... ok
test result: ok. 4 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out
ok 61 fuchsia-pkg://fuchsia.com/archivist_integration_tests#meta/logs_integration_rust_tests.cmx (1.04732004s)
`
	want := `
        [
                {
                        "display_name": "listen_for_klog",
                        "suite_name": "",
                        "case_name": "listen_for_klog",
                        "status": "Pass",
                        "duration_nanos": 0,
                        "format": "Rust"
                },
                {
                        "display_name": "listen_for_syslog",
                        "suite_name": "",
                        "case_name": "listen_for_syslog",
                        "status": "Pass",
                        "duration_nanos": 0,
                        "format": "Rust"
                },
                {
                        "display_name": "listen_for_klog_routed_stdio",
                        "suite_name": "",
                        "case_name": "listen_for_klog_routed_stdio",
                        "status": "Pass",
                        "duration_nanos": 0,
                        "format": "Rust"
                },
                {
                        "display_name": "test_observer_stop_api",
                        "suite_name": "",
                        "case_name": "test_observer_stop_api",
                        "status": "Pass",
                        "duration_nanos": 0,
                        "format": "Rust"
                }
        ]
`
	testCase(t, stdout, want)
}
