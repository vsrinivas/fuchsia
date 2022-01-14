// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"bytes"
	"encoding/json"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

func compactJSON(jsonBytes []byte) []byte {
	buffer := bytes.NewBuffer([]byte{})
	json.Compact(buffer, jsonBytes)
	return buffer.Bytes()
}

func indentJSON(jsonBytes []byte) []byte {
	buffer := bytes.NewBuffer([]byte{})
	json.Indent(buffer, jsonBytes, "", "\t")
	return buffer.Bytes()
}

func testCase(t *testing.T, stdout string, want string) {
	t.Helper()
	actual, _ := json.Marshal(Parse([]byte(stdout)))
	if !bytes.Equal(actual, compactJSON([]byte(want))) {
		actualIndented := string(indentJSON(actual))
		wantIndented := string(indentJSON([]byte(want)))
		t.Errorf("Parse(stdout) = `\n%v\n`; want `\n%v\n`", actualIndented, wantIndented)
	}
}

func testCaseCmp(t *testing.T, stdout string, want []runtests.TestCaseResult) {
	r := Parse([]byte(stdout))
	if diff := cmp.Diff(want, r, cmpopts.SortSlices(func(a, b runtests.TestCaseResult) bool { return a.DisplayName < b.DisplayName })); diff != "" {
		t.Errorf("Found mismatch in %s (-want +got):\n%s", stdout, diff)
	}
}

func TestParseEmpty(t *testing.T) {
	testCaseCmp(t, "", []runtests.TestCaseResult{})
}

func TestParseInvalid(t *testing.T) {
	stdout := `
Mary had a little lamb
Its fleece was white as snow
And everywhere that Mary went
The lamb was sure to go
`
	testCaseCmp(t, stdout, []runtests.TestCaseResult{})
}

// If no test cases can be parsed, the output should be an empty slice, not a
// nil slice, so it gets serialized as an empty JSON array instead of as null.
func TestParseNoTestCases(t *testing.T) {
	testCase(t, "non-test output", "[]")
}

func TestParseTrfTest(t *testing.T) {
	stdout := `
Running test 'fuchsia-pkg://fuchsia.com/f2fs-fs-tests#meta/f2fs-unittest.cm'
[RUNNING]	BCacheTest.Trim
[PASSED]	BCacheTest.Trim
[RUNNING]	BCacheTest.Exception
[00371.417610][942028][942030][<root>] ERROR: [src/storage/f2fs/bcache.cc(45)] Invalid device size
[00371.417732][942028][942030][<root>] ERROR: [src/storage/f2fs/bcache.cc(52)] Block count overflow
[PASSED]	BCacheTest.Exception
[RUNNING]	CheckpointTest.Version
[PASSED]	CheckpointTest.Version
[stdout - BCacheTest.Trim]
magic [0xf2f52010 : 4076150800]
[stdout - BCacheTest.Trim]
major_ver [0x1 : 1]
[stdout - BCacheTest.Trim]
minor_ver [0x0 : 0]
[RUNNING]	FormatFilesystemTest.MkfsOptionsLabel
[00402.723886][969765][969767][<root>] INFO: [mkfs.cc(894)] This device doesn't support TRIM
[00402.743120][969765][969767][<root>] INFO: [mkfs.cc(894)] This device doesn't support TRIM
[stderr - FormatFilesystemTest.MkfsOptionsLabel]
ERROR: label length should be less than 16.
[PASSED]	FormatFilesystemTest.MkfsOptionsLabel
[RUNNING]	NodeManagerTest.TruncateExceptionCase
[stderr - NodeManagerTest.TruncateExceptionCase]
Error reading test result:File(read call failed: A FIDL client's channel to the service (anonymous) File was closed: PEER_CLOSED
[stderr - NodeManagerTest.TruncateExceptionCase]
[stderr - NodeManagerTest.TruncateExceptionCase]
Caused by:
[stderr - NodeManagerTest.TruncateExceptionCase]
    0: A FIDL client's channel to the service (anonymous) File was closed: PEER_CLOSED
[stderr - NodeManagerTest.TruncateExceptionCase]
    1: PEER_CLOSED)
[FAILED]	NodeManagerTest.TruncateExceptionCase
[RUNNING]	VnodeTest.TruncateExceptionCase
[stderr - VnodeTest.TruncateExceptionCase]
Test exited abnormally
[FAILED]	VnodeTest.TruncateExceptionCase
Failed tests: NodeManagerTest.TruncateExceptionCase, VnodeTest.TruncateExceptionCase
[duration - virtualization::virtualization_netdevice::remove_network]:	Still running after 60 seconds
[TIMED_OUT]	virtualization::virtualization_netdevice::remove_network
100 out of 102 tests passed...
fuchsia-pkg://fuchsia.com/f2fs-fs-tests#meta/f2fs-unittest.cm completed with result: FAILED
One or more test runs failed.
`
	want := []runtests.TestCaseResult{
		{
			DisplayName: "BCacheTest.Trim",
			CaseName:    "BCacheTest.Trim",
			Status:      runtests.TestSuccess,
			Format:      "FTF",
		}, {
			DisplayName: "BCacheTest.Exception",
			CaseName:    "BCacheTest.Exception",
			Status:      runtests.TestSuccess,
			Format:      "FTF",
		}, {
			DisplayName: "CheckpointTest.Version",
			CaseName:    "CheckpointTest.Version",
			Status:      runtests.TestSuccess,
			Format:      "FTF",
		}, {
			DisplayName: "FormatFilesystemTest.MkfsOptionsLabel",
			CaseName:    "FormatFilesystemTest.MkfsOptionsLabel",
			Status:      runtests.TestSuccess,
			Format:      "FTF",
		}, {
			DisplayName: "NodeManagerTest.TruncateExceptionCase",
			CaseName:    "NodeManagerTest.TruncateExceptionCase",
			Status:      runtests.TestFailure,
			Format:      "FTF",
			FailReason:  "Error reading test result:File(read call failed: A FIDL client's channel to the service (anonymous) File was closed: PEER_CLOSED",
		}, {
			DisplayName: "VnodeTest.TruncateExceptionCase",
			CaseName:    "VnodeTest.TruncateExceptionCase",
			Status:      runtests.TestFailure,
			Format:      "FTF",
			FailReason:  "Test exited abnormally",
		}, {
			DisplayName: "virtualization::virtualization_netdevice::remove_network",
			CaseName:    "virtualization::virtualization_netdevice::remove_network",
			Status:      runtests.TestAborted,
			Format:      "FTF",
		},
	}
	testCaseCmp(t, stdout, want)
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
	want := []runtests.TestCaseResult{
		{
			DisplayName: "SynonymDictTest.IsInitializedEmpty",
			SuiteName:   "SynonymDictTest",
			CaseName:    "IsInitializedEmpty",
			Status:      runtests.TestSuccess,
			Duration:    4000000,
			Format:      "GoogleTest",
		}, {
			DisplayName: "SynonymDictTest.ReadingEmptyFileReturnsFalse",
			SuiteName:   "SynonymDictTest",
			CaseName:    "ReadingEmptyFileReturnsFalse",
			Status:      runtests.TestSuccess,
			Duration:    3000000,
			Format:      "GoogleTest",
		}, {
			DisplayName: "SynonymDictTest.ReadingNonexistentFileReturnsFalse",
			SuiteName:   "SynonymDictTest",
			CaseName:    "ReadingNonexistentFileReturnsFalse",
			Status:      runtests.TestSuccess,
			Duration:    4000000,
			Format:      "GoogleTest",
		}, {
			DisplayName: "SynonymDictTest.LoadDictionary",
			SuiteName:   "SynonymDictTest",
			CaseName:    "LoadDictionary",
			Status:      runtests.TestSuccess,
			Duration:    4000000,
			Format:      "GoogleTest",
		}, {
			DisplayName: "SynonymDictTest.GetSynonymsReturnsListOfWords",
			SuiteName:   "SynonymDictTest",
			CaseName:    "GetSynonymsReturnsListOfWords",
			Status:      runtests.TestSuccess,
			Duration:    4000000,
			Format:      "GoogleTest",
		}, {
			DisplayName: "SynonymDictTest.GetSynonymsWhenNoSynonymsAreAvailable",
			SuiteName:   "SynonymDictTest",
			CaseName:    "GetSynonymsWhenNoSynonymsAreAvailable",
			Status:      runtests.TestSuccess,
			Duration:    4000000,
			Format:      "GoogleTest",
		}, {
			DisplayName: "SynonymDictTest.AllWordsAreSynonymsOfEachOther",
			SuiteName:   "SynonymDictTest",
			CaseName:    "AllWordsAreSynonymsOfEachOther",
			Status:      runtests.TestSuccess,
			Duration:    4000000,
			Format:      "GoogleTest",
		}, {
			DisplayName: "SynonymDictTest.GetSynonymsReturnsListOfWordsWithStubs",
			SuiteName:   "SynonymDictTest",
			CaseName:    "GetSynonymsReturnsListOfWordsWithStubs",
			Status:      runtests.TestFailure,
			Duration:    4000000,
			Format:      "GoogleTest",
		}, {
			DisplayName: "SynonymDictTest.CompoundWordBug",
			SuiteName:   "SynonymDictTest",
			CaseName:    "CompoundWordBug",
			Status:      runtests.TestSkipped,
			Duration:    4000000,
			Format:      "GoogleTest",
		},
	}
	testCaseCmp(t, stdout, want)
}

func TestParseGo(t *testing.T) {
	stdout := `
2020/06/17 18:15:06.096179 testrunner DEBUG: starting: [host_x64/fake_tests --test.timeout 5m -test.v]
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
=== RUN   TestAdd
=== RUN   TestAdd/add_foo
=== RUN   TestAdd/add_bar
=== RUN   TestAdd/add_baz
--- PASS: TestAdd (0.00s)
    --- PASS: TestAdd/add_foo (0.00s)
    --- PASS: TestAdd/add_bar (0.00s)
		--- PASS: TestAdd/add_baz (0.00s)
ok 8 host_x64/fake_tests (4.378744489s)
`
	want := []runtests.TestCaseResult{
		{
			DisplayName: "TestParseEmpty",
			CaseName:    "TestParseEmpty",
			Status:      runtests.TestSuccess,
			Duration:    10000000,
			Format:      "Go",
		}, {
			DisplayName: "TestParseInvalid",
			CaseName:    "TestParseInvalid",
			Status:      runtests.TestSuccess,
			Duration:    20000000,
			Format:      "Go",
		}, {
			DisplayName: "TestParseGoogleTest",
			CaseName:    "TestParseGoogleTest",
			Status:      runtests.TestFailure,
			Duration:    3000000000,
			Format:      "Go",
		}, {
			DisplayName: "TestFail",
			CaseName:    "TestFail",
			Status:      runtests.TestFailure,
			Format:      "Go",
		}, {
			DisplayName: "TestSkip",
			CaseName:    "TestSkip",
			Status:      runtests.TestSkipped,
			Format:      "Go",
		}, {
			DisplayName: "TestAdd",
			CaseName:    "TestAdd",
			Status:      runtests.TestSuccess,
			Format:      "Go",
		}, {
			DisplayName: "TestAdd/add_foo",
			SuiteName:   "TestAdd",
			CaseName:    "add_foo",
			Status:      runtests.TestSuccess,
			Format:      "Go",
		}, {
			DisplayName: "TestAdd/add_bar",
			SuiteName:   "TestAdd",
			CaseName:    "add_bar",
			Status:      runtests.TestSuccess,
			Format:      "Go",
		}, {
			DisplayName: "TestAdd/add_baz",
			SuiteName:   "TestAdd",
			CaseName:    "add_baz",
			Status:      runtests.TestSuccess,
			Format:      "Go",
		},
	}
	testCaseCmp(t, stdout, want)
}

func TestParseGoPanic(t *testing.T) {
	stdout := `
=== RUN   TestReboot
Running /tmp/qemu-distro868073415/bin/qemu-system-x86_64 [-initrd /usr/local/google/home/curtisgalloway/src/fuchsia/out/core.x64/fuchsia.zbi -kernel /usr/local/google/home/curtisgalloway/src/fuchsia/out/core.x64/host_x64/test_data/qemu/multiboot.bin -nographic -smp 4,threads=2 -trace enable=vm_state_notify -machine q35 -device isa-debug-exit,iobase=0xf4,iosize=0x04 -cpu Haswell,+smap,-check,-fsgsbase -m 8192 -net none -append kernel.serial=legacy kernel.entropy-mixin=1420bb81dc0396b37cc2d0aa31bb2785dadaf9473d0780ecee1751afb5867564 kernel.halt-on-panic=true devmgr.log-to-debuglog]
Checking for QEMU boot...
9234@1592444858.702846:vm_state_notify running 1 reason 9
SeaBIOS (version rel-1.11.1-0-g0551a4be2c-prebuilt.qemu-project.org)
panic: test timed out after 1s

goroutine 52 [running]:
testing.(*M).startAlarm.func1()
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/testing/testing.go:1459 +0xdf
created by time.goFunc
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/time/sleep.go:168 +0x44

goroutine 1 [chan receive]:
testing.(*T).Run(0xc00014c120, 0x570c48, 0xa, 0x57a1a8, 0x47ebc6)
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/testing/testing.go:1043 +0x37e
testing.runTests.func1(0xc00014c000)
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/testing/testing.go:1284 +0x78
testing.tRunner(0xc00014c000, 0xc000088e10)
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/testing/testing.go:991 +0xdc
testing.runTests(0xc000134020, 0x6896d0, 0x1, 0x1, 0x0)
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/testing/testing.go:1282 +0x2a7
testing.(*M).Run(0xc000148000, 0x0)
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/testing/testing.go:1199 +0x15f
main.main()
	_testmain.go:44 +0x135

goroutine 19 [IO wait]:
internal/poll.runtime_pollWait(0x7f1d4a735d88, 0x72, 0xffffffffffffffff)
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/runtime/netpoll.go:203 +0x55
internal/poll.(*pollDesc).wait(0xc0004f6258, 0x72, 0xf01, 0xfee, 0xffffffffffffffff)
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/internal/poll/fd_poll_runtime.go:87 +0x45
internal/poll.(*pollDesc).waitRead(...)
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/internal/poll/fd_poll_runtime.go:92
internal/poll.(*FD).Read(0xc0004f6240, 0xc0005ca012, 0xfee, 0xfee, 0x0, 0x0, 0x0)
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/internal/poll/fd_unix.go:169 +0x19b
os.(*File).read(...)
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/os/file_unix.go:263
os.(*File).Read(0xc000010040, 0xc0005ca012, 0xfee, 0xfee, 0x1, 0x0, 0x0)
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/os/file.go:116 +0x71
bufio.(*Reader).fill(0xc0004f63c0)
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/bufio/bufio.go:100 +0x103
bufio.(*Reader).ReadSlice(0xc0004f63c0, 0x47040a, 0x689dc0, 0xc0005c4080, 0x1, 0xc00015bd40, 0x4b2ee1)
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/bufio/bufio.go:359 +0x3d
bufio.(*Reader).ReadBytes(0xc0004f63c0, 0xa, 0x5734e6, 0x15, 0xffffffffffffffff, 0x46, 0x0)
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/bufio/bufio.go:438 +0x7a
bufio.(*Reader).ReadString(...)
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/bufio/bufio.go:475
go.fuchsia.dev/fuchsia/src/testing/qemu.(*Instance).checkForLogMessage(0xc000134460, 0xc0004f63c0, 0x5734e6, 0x15, 0x573893, 0x16)
	/usr/local/google/home/curtisgalloway/src/fuchsia/out/core.x64/host_x64/gen/gopaths/reboot_tests/src/go.fuchsia.dev/fuchsia/src/testing/qemu/qemu.go:497 +0x46
go.fuchsia.dev/fuchsia/src/testing/qemu.(*Instance).WaitForLogMessage(...)
	/usr/local/google/home/curtisgalloway/src/fuchsia/out/core.x64/host_x64/gen/gopaths/reboot_tests/src/go.fuchsia.dev/fuchsia/src/testing/qemu/qemu.go:432
go.fuchsia.dev/fuchsia/src/tests/reboot.TestReboot(0xc00014c120)
	/usr/local/google/home/curtisgalloway/src/fuchsia/out/core.x64/host_x64/gen/gopaths/reboot_tests/src/go.fuchsia.dev/fuchsia/src/tests/reboot/reboot_test.go:52 +0x36c
testing.tRunner(0xc00014c120, 0x57a1a8)
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/testing/testing.go:991 +0xdc
created by testing.(*T).Run
	/usr/local/google/home/curtisgalloway/src/fuchsia/prebuilt/third_party/go/linux-x64/src/testing/testing.go:1042 +0x357
	`
	want := []runtests.TestCaseResult{
		{
			DisplayName: "TestReboot",
			CaseName:    "TestReboot",
			Status:      runtests.TestFailure,
			Duration:    1000000000,
			Format:      "Go",
		},
	}
	testCaseCmp(t, stdout, want)
}

func TestParseRust(t *testing.T) {
	stdout := `
running 3 tests
test tests::ignored_test ... ignored
[stdout - legacy_test]
test tests::test_add_hundred ... ok
[stdout - legacy_test]
test tests::test_add ... FAILED
[stdout - legacy_test]
test tests::test_substract ... FAILED
[stdout - legacy_test]
[stdout - legacy_test]
failures:
[stdout - legacy_test]
[stdout - legacy_test]
---- tests::test_add_hundred stdout ----
[stdout - legacy_test]
---- tests::test_add_hundred stderr ----
[stdout - legacy_test]
booooo I printed an error, but it doesn't count as fail reason
---- tests::test_add stdout ----
[stdout - legacy_test]
---- tests::test_add stderr ----
[stdout - legacy_test]
thread 'main' panicked at 'assertion failed: ` + "`(left != right)`" + `
[stdout - legacy_test]
  left: ` + "`ObjectType(PORT)`" + `,
[stdout - legacy_test]
  right: ` + "`ObjectType(PORT)`', ../../src/lib/zircon/rust/src/channel.rs:761:9`" + `
[stdout - legacy_test]
stack backtrace:
[stdout - legacy_test]
{{{reset}}}
[stdout - legacy_test]
{{{module:0x0::elf:cb02c721da2e5287}}}
[stdout - legacy_test]
{{{mmap:0x2de1be9a000:0x11a5c:load:0x0:r:0x0}}}
[stdout - legacy_test]
[stdout - legacy_test]
---- tests::test_substract stdout ----
[stdout - legacy_test]
---- tests::test_substract stderr ----
[stdout - legacy_test]
thread 'main' panicked at 'assertion failed: ` + "`(left != right)`" + `
[stdout - legacy_test]
  left: ` + "`Err((5, 0))`" + `,
[stdout - legacy_test]
  right: ` + "`Err((5, 0))`" + `', ../../src/lib/zircon/rust/src/channel.rs:783:9
[stdout - legacy_test]
stack backtrace:
[stdout - legacy_test]
{{{reset}}}
[stdout - legacy_test]
{{{module:0x0::elf:cb02c721da2e5287}}}
[stdout - legacy_test]
{{{mmap:0x3441843f000:0x11a5c:load:0x0:r:0x0}}}
[stdout - legacy_test]
{{{mmap:0x34418451000:0x18f90:load:0x0:rx:0x12000}}}
[stdout - legacy_test]
failures:
[stdout - legacy_test]
    test tests::test_add
[stdout - legacy_test]
    test tests::test_substract
[stdout - legacy_test]
[stdout - legacy_test]
test result: FAILED. 1 passed; 2 failed; 1 ignored; 0 measured; 0 filtered out; finished in 5.30s
[stdout - legacy_test]
[FAILED]	legacy_test
Failed tests: legacy_test
0 out of 1 tests passed...
fuchsia-pkg://fuchsia.com/fuchsiatests#meta/some-tests.cm completed with result: FAILED
One or more test runs failed.`

	want := []runtests.TestCaseResult{
		{
			DisplayName: "tests::ignored_test",
			SuiteName:   "tests",
			CaseName:    "ignored_test",
			Status:      runtests.TestSkipped,
			Format:      "Rust",
		}, {
			DisplayName: "tests::test_add_hundred",
			SuiteName:   "tests",
			CaseName:    "test_add_hundred",
			Status:      runtests.TestSuccess,
			Format:      "Rust",
		}, {
			DisplayName: "tests::test_add",
			SuiteName:   "tests",
			CaseName:    "test_add",
			Status:      runtests.TestFailure,
			Format:      "Rust",
			FailReason:  "thread 'main' panicked at 'assertion failed: `(left != right)`\n  left: `ObjectType(PORT)`,\n  right: `ObjectType(PORT)`', ../../src/lib/zircon/rust/src/channel.rs:761:9`",
		}, {
			DisplayName: "tests::test_substract",
			SuiteName:   "tests",
			CaseName:    "test_substract",
			Status:      runtests.TestFailure,
			Format:      "Rust",
			FailReason:  "thread 'main' panicked at 'assertion failed: `(left != right)`\n  left: `Err((5, 0))`,\n  right: `Err((5, 0))`', ../../src/lib/zircon/rust/src/channel.rs:783:9",
		},
	}
	testCaseCmp(t, stdout, want)
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

	want := []runtests.TestCaseResult{
		{
			DisplayName: "minfs_truncate_tests.TestTruncateSmall",
			SuiteName:   "minfs_truncate_tests",
			CaseName:    "TestTruncateSmall",
			Duration:    1000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_truncate_tests.(TestTruncateLarge\u003c1 \u003c\u003c 10, 1000\u003e)",
			SuiteName:   "minfs_truncate_tests",
			CaseName:    "(TestTruncateLarge\u003c1 \u003c\u003c 10, 1000\u003e)",
			Duration:    20414000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_truncate_tests.(TestTruncateLarge\u003c1 \u003c\u003c 15, 500\u003e)",
			SuiteName:   "minfs_truncate_tests",
			CaseName:    "(TestTruncateLarge\u003c1 \u003c\u003c 15, 500\u003e)",
			Duration:    10012000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_truncate_tests.(TestTruncateLarge\u003c1 \u003c\u003c 20, 500\u003e)",
			SuiteName:   "minfs_truncate_tests",
			CaseName:    "(TestTruncateLarge\u003c1 \u003c\u003c 20, 500\u003e)",
			Duration:    10973000000,
			Status:      runtests.TestFailure,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_truncate_tests.(TestTruncateLarge\u003c1 \u003c\u003c 25, 500\u003e)",
			SuiteName:   "minfs_truncate_tests",
			CaseName:    "(TestTruncateLarge\u003c1 \u003c\u003c 25, 500\u003e)",
			Duration:    0,
			Status:      runtests.TestSkipped,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_sparse_tests.(test_sparse\u003c0, 0, kBlockSize\u003e)",
			SuiteName:   "minfs_sparse_tests",
			CaseName:    "(test_sparse\u003c0, 0, kBlockSize\u003e)",
			Duration:    19000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_sparse_tests.(test_sparse\u003ckBlockSize / 2, 0, kBlockSize\u003e)",
			SuiteName:   "minfs_sparse_tests",
			CaseName:    "(test_sparse\u003ckBlockSize / 2, 0, kBlockSize\u003e)",
			Duration:    20000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_sparse_tests.(test_sparse\u003ckBlockSize / 2, kBlockSize, kBlockSize\u003e)",
			SuiteName:   "minfs_sparse_tests",
			CaseName:    "(test_sparse\u003ckBlockSize / 2, kBlockSize, kBlockSize\u003e)",
			Duration:    19000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_sparse_tests.(test_sparse\u003ckBlockSize, 0, kBlockSize\u003e)",
			SuiteName:   "minfs_sparse_tests",
			CaseName:    "(test_sparse\u003ckBlockSize, 0, kBlockSize\u003e)",
			Duration:    19000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_sparse_tests.(test_sparse\u003ckBlockSize, kBlockSize / 2, kBlockSize\u003e)",
			SuiteName:   "minfs_sparse_tests",
			CaseName:    "(test_sparse\u003ckBlockSize, kBlockSize / 2, kBlockSize\u003e)",
			Duration:    19000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_sparse_tests.(test_sparse\u003ckBlockSize * kDirectBlocks, kBlockSize * kDirectBlocks - kBlockSize, kBlockSize * 2\u003e)",
			SuiteName:   "minfs_sparse_tests",
			CaseName:    "(test_sparse\u003ckBlockSize * kDirectBlocks, kBlockSize * kDirectBlocks - kBlockSize, kBlockSize * 2\u003e)",
			Duration:    20000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_sparse_tests.(test_sparse\u003ckBlockSize * kDirectBlocks, kBlockSize * kDirectBlocks - kBlockSize, kBlockSize * 32\u003e)",
			SuiteName:   "minfs_sparse_tests",
			CaseName:    "(test_sparse\u003ckBlockSize * kDirectBlocks, kBlockSize * kDirectBlocks - kBlockSize, kBlockSize * 32\u003e)",
			Duration:    24000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_sparse_tests.(test_sparse\u003ckBlockSize * kDirectBlocks + kBlockSize, kBlockSize * kDirectBlocks - kBlockSize, kBlockSize * 32\u003e)",
			SuiteName:   "minfs_sparse_tests",
			CaseName:    "(test_sparse\u003ckBlockSize * kDirectBlocks + kBlockSize, kBlockSize * kDirectBlocks - kBlockSize, kBlockSize * 32\u003e)",
			Duration:    24000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_sparse_tests.(test_sparse\u003ckBlockSize * kDirectBlocks + kBlockSize, kBlockSize * kDirectBlocks + 2 * kBlockSize, kBlockSize * 32\u003e)",
			SuiteName:   "minfs_sparse_tests",
			CaseName:    "(test_sparse\u003ckBlockSize * kDirectBlocks + kBlockSize, kBlockSize * kDirectBlocks + 2 * kBlockSize, kBlockSize * 32\u003e)",
			Duration:    25000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_rw_workers_test.TestWorkSingleThread",
			SuiteName:   "minfs_rw_workers_test",
			CaseName:    "TestWorkSingleThread",
			Duration:    40920000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_maxfile_tests.test_maxfile",
			SuiteName:   "minfs_maxfile_tests",
			CaseName:    "test_maxfile",
			Duration:    62243000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_directory_tests.TestDirectoryLarge",
			SuiteName:   "minfs_directory_tests",
			CaseName:    "TestDirectoryLarge",
			Duration:    3251000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_directory_tests.TestDirectoryReaddir",
			SuiteName:   "minfs_directory_tests",
			CaseName:    "TestDirectoryReaddir",
			Duration:    69000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_directory_tests.TestDirectoryReaddirLarge",
			SuiteName:   "minfs_directory_tests",
			CaseName:    "TestDirectoryReaddirLarge",
			Duration:    6414000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		}, {
			DisplayName: "minfs_basic_tests.test_basic",
			SuiteName:   "minfs_basic_tests",
			CaseName:    "test_basic",
			Duration:    21000000,
			Status:      runtests.TestSuccess,
			Format:      "Zircon utest",
		},
	}
	testCaseCmp(t, stdout, want)
}

// Regression test for fxbug.dev/51327
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
	want := []runtests.TestCaseResult{
		{
			DisplayName: "UltrasoundTest.CreateRenderer",
			SuiteName:   "UltrasoundTest",
			CaseName:    "CreateRenderer",
			Duration:    964000000,
			Status:      runtests.TestSuccess,
			Format:      "GoogleTest",
		}, {
			DisplayName: "UltrasoundTest.RendererDoesNotSupportSetPcmStreamType",
			SuiteName:   "UltrasoundTest",
			CaseName:    "RendererDoesNotSupportSetPcmStreamType",
			Duration:    142000000,
			Status:      runtests.TestSuccess,
			Format:      "GoogleTest",
		}, {
			DisplayName: "UltrasoundTest.RendererDoesNotSupportSetUsage",
			SuiteName:   "UltrasoundTest",
			CaseName:    "RendererDoesNotSupportSetUsage",
			Duration:    112000000,
			Status:      runtests.TestSuccess,
			Format:      "GoogleTest",
		}, {
			DisplayName: "UltrasoundTest.RendererDoesNotSupportBindGainControl",
			SuiteName:   "UltrasoundTest",
			CaseName:    "RendererDoesNotSupportBindGainControl",
			Duration:    151000000,
			Status:      runtests.TestSuccess,
			Format:      "GoogleTest",
		}, {
			DisplayName: "UltrasoundTest.RendererDoesNotSupportSetReferenceClock",
			SuiteName:   "UltrasoundTest",
			CaseName:    "RendererDoesNotSupportSetReferenceClock",
			Duration:    143000000,
			Status:      runtests.TestSuccess,
			Format:      "GoogleTest",
		}, {
			DisplayName: "UltrasoundTest.CreateCapturer",
			SuiteName:   "UltrasoundTest",
			CaseName:    "CreateCapturer",
			Duration:    32000000,
			Status:      runtests.TestSuccess,
			Format:      "GoogleTest",
		}, {
			DisplayName: "UltrasoundTest.CapturerDoesNotSupportSetPcmStreamType",
			SuiteName:   "UltrasoundTest",
			CaseName:    "CapturerDoesNotSupportSetPcmStreamType",
			Duration:    51000000,
			Status:      runtests.TestSuccess,
			Format:      "GoogleTest",
		}, {
			DisplayName: "UltrasoundTest.CapturerDoesNotSupportSetUsage",
			SuiteName:   "UltrasoundTest",
			CaseName:    "CapturerDoesNotSupportSetUsage",
			Duration:    42000000,
			Status:      runtests.TestSuccess,
			Format:      "GoogleTest",
		}, {
			DisplayName: "UltrasoundTest.CapturerDoesNotSupportBindGainControl",
			SuiteName:   "UltrasoundTest",
			CaseName:    "CapturerDoesNotSupportBindGainControl",
			Duration:    52000000,
			Status:      runtests.TestSuccess,
			Format:      "GoogleTest",
		}, {
			DisplayName: "UltrasoundTest.CapturerDoesNotSupportSetReferenceClock",
			SuiteName:   "UltrasoundTest",
			CaseName:    "CapturerDoesNotSupportSetReferenceClock",
			Duration:    41000000,
			Status:      runtests.TestSuccess,
			Format:      "GoogleTest",
		},
	}
	testCaseCmp(t, stdout, want)
}

// Regression test for fxbug.dev/52363
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
	want := []runtests.TestCaseResult{
		{
			DisplayName: "listen_for_klog",
			CaseName:    "listen_for_klog",
			Status:      runtests.TestSuccess,
			Format:      "Rust",
		}, {
			DisplayName: "listen_for_syslog",
			CaseName:    "listen_for_syslog",
			Status:      runtests.TestSuccess,
			Format:      "Rust",
		}, {
			DisplayName: "listen_for_klog_routed_stdio",
			CaseName:    "listen_for_klog_routed_stdio",
			Status:      runtests.TestSuccess,
			Format:      "Rust",
		}, {
			DisplayName: "test_observer_stop_api",
			CaseName:    "test_observer_stop_api",
			Status:      runtests.TestSuccess,
			Format:      "Rust",
		},
	}
	testCaseCmp(t, stdout, want)
}

func TestTRFLegacyTest(t *testing.T) {
	stdout := `
Running test 'fuchsia-pkg://fuchsia.com/vkreadback#meta/vkreadback.cm'
[RUNNING]	legacy_test
[stdout - legacy_test]
[==========] Running 5 tests from 1 test suite.
[stdout - legacy_test]
[----------] Global test environment set-up.
[stdout - legacy_test]
[----------] 5 tests from Vulkan
[stdout - legacy_test]
[ RUN      ] Vulkan.Readback
[stdout - legacy_test]
****** Test Failed! 4096 mismatches
[stdout - legacy_test]
../../src/graphics/tests/vkreadback/main.cc:33: Failure
[stdout - legacy_test]
Value of: test.Readback()
[stdout - legacy_test]
	Actual: false
[stdout - legacy_test]
Expected: true
[stdout - legacy_test]
[  FAILED  ] Vulkan.Readback (132 ms)
[stdout - legacy_test]
[ RUN      ] Vulkan.ManyReadback
[stderr - legacy_test]
Clear Color Value Mismatch at index 0 - expected 0xbf8000ff, got 0xabababab
[stderr - legacy_test]
Clear Color Value Mismatch at index 1 - expected 0xbf8000ff, got 0xabababab
[stderr - legacy_test]
Clear Color Value Mismatch at index 2 - expected 0xbf8000ff, got 0xabababab
[stderr - legacy_test]
Clear Color Value Mismatch at index 3 - expected 0xbf8000ff, got 0xabababab
[stdout - legacy_test]
****** Test Failed! 4096 mismatches
[stdout - legacy_test]
../../src/graphics/tests/vkreadback/main.cc:45: Failure
[stdout - legacy_test]
Value of: test->Readback()
[stdout - legacy_test]
  Actual: false
[stdout - legacy_test]
Expected: true
[stdout - legacy_test]
[  FAILED  ] Vulkan.ManyReadback (9078 ms)
[stdout - legacy_test]
[ RUN      ] Vulkan.ReadbackLoopWithFenceWaitThread
[stderr - legacy_test]
Clear Color Value Mismatch at index 0 - expected 0xbf8000ff, got 0xabababab
[stderr - legacy_test]
Clear Color Value Mismatch at index 1 - expected 0xbf8000ff, got 0xabababab
[stdout - legacy_test]
****** Test Failed! 4096 mismatches
[stdout - legacy_test]
../../src/graphics/tests/vkreadback/main.cc:98: Failure
[stdout - legacy_test]
Value of: test.Readback()
[stdout - legacy_test]
  Actual: false
[stdout - legacy_test]
Expected: true
[stdout - legacy_test]
[  FAILED  ] Vulkan.ReadbackLoopWithFenceWaitThread (340 ms)
`
	want := []runtests.TestCaseResult{}
	testCaseCmp(t, stdout, want)
}

func TestParseDartSystemTest(t *testing.T) {
	stdout := `
[----------] Test results JSON:
{
  "bqTableName": "e2etest",
  "bqDatasetName": "e2e_test_data",
  "bqProjectName": "fuchsia-infra",
  "buildID": "8880180380045754528",
  "startTime": "2020-05-16 02:44:33.519488",
  "buildBucketInfo": {
    "user": null,
    "botID": "fuchsia-internal-try-n1-1-ssd0-us-central1-c-37-za5b",
    "builderName": "foo",
    "buildID": "8880180380045754528",
    "changeNumber": null,
    "gcsBucket": "paper-crank-rogue-raft",
    "reason": "",
    "repository": "foo",
    "startTime": "2020-05-16 02:44:33.519488"
  },
  "testGroups": [
    {
      "name": "foo_test/group1",
      "result": "PASSED",
      "startTime": "2020-05-16 03:17:20.987638",
      "loginMode": "NOT_RUN",
      "retries": 0,
      "durationInSeconds": 87,
      "testCases": [
        {
          "name": "test1",
          "result": "PASSED",
          "startTime": "2020-05-16 03:17:25.745931",
          "loginMode": "NOT_RUN",
          "retries": 0,
          "durationInSeconds": 52,
          "customFields": [
            {
              "key": "device_name",
              "value": "paper-crank-rogue-raft"
            },
            {
              "key": "transcript",
              "value": "foo"
            }
          ]
        },
        {
          "name": "test2",
          "result": "FAILED",
          "startTime": "2020-05-16 03:18:18.197664",
          "loginMode": "NOT_RUN",
          "retries": 0,
          "durationInSeconds": 30,
          "customFields": [
            {
              "key": "device_name",
              "value": "paper-crank-rogue-raft"
            },
            {
              "key": "transcript",
              "value": "foo"
            }
          ]
        }
      ]
    },
    {
      "name": "foo_test/group2",
      "result": "PASSED",
      "startTime": "2020-05-16 03:17:18.291768",
      "loginMode": "UNKNOWN",
      "retries": 0,
      "durationInSeconds": 90,
      "testCases": []
    }
  ]
}
`
	want := []runtests.TestCaseResult{
		{
			DisplayName: "foo_test/group1.test1",
			SuiteName:   "foo_test/group1",
			CaseName:    "test1",
			Duration:    52000000000,
			Status:      runtests.TestSuccess,
			Format:      "dart_system_test",
		}, {
			DisplayName: "foo_test/group1.test2",
			SuiteName:   "foo_test/group1",
			CaseName:    "test2",
			Duration:    30000000000,
			Status:      runtests.TestFailure,
			Format:      "dart_system_test",
		},
	}
	testCaseCmp(t, stdout, want)
}

func TestParseVulkanCts(t *testing.T) {
	stdout := `
Writing test log into /data/TestResults.qpa
dEQP Core git-6c86ad6eade572a70482771aa1c4d466fe7106ef (0x6c86ad6e) starting..
  target implementation = 'Fuchsia'
Test case 'dEQP-VK.renderpass.suballocation.multisample.r32g32_uint.samples_8'..
Pass (Pass)
Test case 'dEQP-VK.renderpass.suballocation.multisample.separate_stencil_usage.d32_sfloat_s8_uint.samples_32.test_stencil'..
NotSupported (Image type not supported at vktRenderPassMultisampleTests.cpp:270)
Test case 'dEQP-VK.renderpass.suballocation.multisample.r32g32_uint.samples_9'..
Fail (bad)
Test case 'dEQP-VK.renderpass.suballocation.multisample.r32g32_uint.samples_10'..
`

	want := []runtests.TestCaseResult{
		{
			DisplayName: "dEQP-VK.renderpass.suballocation.multisample.r32g32_uint.samples_8",
			SuiteName:   "dEQP-VK.renderpass.suballocation.multisample.r32g32_uint",
			CaseName:    "samples_8",
			Status:      runtests.TestSuccess,
			Format:      "VulkanCtsTest",
		}, {
			DisplayName: "dEQP-VK.renderpass.suballocation.multisample.separate_stencil_usage.d32_sfloat_s8_uint.samples_32.test_stencil",
			SuiteName:   "dEQP-VK.renderpass.suballocation.multisample.separate_stencil_usage.d32_sfloat_s8_uint.samples_32",
			CaseName:    "test_stencil",
			Status:      runtests.TestSkipped,
			Format:      "VulkanCtsTest",
		}, {
			DisplayName: "dEQP-VK.renderpass.suballocation.multisample.r32g32_uint.samples_9",
			SuiteName:   "dEQP-VK.renderpass.suballocation.multisample.r32g32_uint",
			CaseName:    "samples_9",
			Status:      runtests.TestFailure,
			Format:      "VulkanCtsTest",
		}, {
			DisplayName: "dEQP-VK.renderpass.suballocation.multisample.r32g32_uint.samples_10",
			SuiteName:   "dEQP-VK.renderpass.suballocation.multisample.r32g32_uint",
			CaseName:    "samples_10",
			Status:      runtests.TestFailure,
			Format:      "VulkanCtsTest",
		},
	}
	testCaseCmp(t, stdout, want)
}
