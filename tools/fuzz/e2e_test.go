// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz_test

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"testing"
	"time"

	"github.com/golang/glog"
	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/fuzz"
)

// To build these tests:
// - fx set core.x64 \
//     --with-base //tools/fuzz:tests \
//     --with-base //tools/fuzz/testing:undercoat-test-fuzzers \
//     --with-base //bundles:tools \
//     --fuzz-with asan && fx build
//
// To run these tests:
// - UNDERCOAT_E2E_TESTS=1 fx test --host undercoat
//
// Or, for an individual test with detailed output:
// - cd tools/fuzz && UNDERCOAT_E2E_TESTS=1 go test -run <test-name> -v -logtostderr

// Note: The bulk of the testing is done in separate functions called by this
// test, but for performance reasons (and because ffx uses a global daemon)
// they currently all run serially on the same instance.
//
// This has an added benefit of being closer to the actual behavior seen when
// being called from ClusterFuzz, where the same instance is often re-used for
// multiple fuzzer invocations.
func TestEndToEnd(t *testing.T) {
	if _, found := os.LookupEnv("UNDERCOAT_E2E_TESTS"); !found {
		t.Skip("skipping end-to-end test; set UNDERCOAT_E2E_TESTS to enable")
	}

	out := runCommandOk(t, "version")
	if m, err := regexp.MatchString(`^v\d+\.\d+\.\d+\n$`, out); err != nil || !m {
		t.Fatalf("unxpected output: %s", out)
	}

	out = runCommandOk(t, "start_instance")
	if m, err := regexp.MatchString(`^\S+\n$`, out); err != nil || !m {
		t.Fatalf("unxpected output: %s", out)
	}

	handle := strings.TrimSuffix(out, "\n")

	defer runCommandOk(t, "stop_instance", "-handle", handle)

	// Fetch debug logs
	log_postboot := runCommandOk(t, "get_logs", "-handle", handle)

	// Check for syslog presence (as done by test_qemu_logs_returned_on_error in ClusterFuzz)
	if !strings.Contains(log_postboot, "{{{reset}}}") {
		t.Fatalf("Post-boot debug log missing expected content:\n%s", out)
	}

	testListFuzzers(t, handle)
	testPrepareFuzzer(t, handle)
	testFuzzWithoutCorpus(t, handle)
	testFuzzWithCorpus(t, handle)
	testMinimize(t, handle)
	testMerge(t, handle)
	testReproWithCrash(t, handle)

	// Ensure that the debug logs have grown in length, to verify that we are
	// properly capturing logs after early boot. The above repro of an ASAN
	// crash is guaranteed to write to the debuglog due to the current
	// definition of `__sanitizer_log_write`. This behavior may change in the
	// future, but for now it is the most straightforward way to trigger a
	// write to the log.
	log_postrun := runCommandOk(t, "get_logs", "-handle", handle)
	if len(log_postrun) <= len(log_postboot) {
		t.Fatalf("Post-run debug log same size as post-boot log (%d bytes)", len(log_postrun))
	}

	testReproWithoutCrash(t, handle)
	testPutGet(t, handle)
	testBulkPut(t, handle)
}

func testListFuzzers(t *testing.T, handle string) {
	// Note: this shows up in the list despite being an example fuzzer because
	// it's currently explicitly allowlisted in fuzzer.go
	oom_fuzzer := "example-fuzzers/out_of_memory_fuzzer"
	out := runCommandOk(t, "list_fuzzers", "-handle", handle)
	if !strings.Contains(out, oom_fuzzer) {
		t.Fatalf("%q fuzzer missing from output:\n%s", oom_fuzzer, out)
	}
}

// Test that prepare_fuzzer affects fuzzer state as expected
func testPrepareFuzzer(t *testing.T, handle string) {
	dir := t.TempDir()
	crash_fuzzer := "example-fuzzers/crash_fuzzer"

	// Ensure put_data will succeed for prepared fuzzer, even if not yet run
	out := runCommandOk(t, "prepare_fuzzer", "-handle", handle, "-fuzzer", crash_fuzzer)
	glog.Info(out)

	tmpFile := path.Join(dir, "autoexec.bat")
	expected := []byte("something")
	if err := os.WriteFile(tmpFile, expected, 0o600); err != nil {
		t.Fatalf("error creating tempfile: %s", err)
	}

	out = runCommandOk(t, "put_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-src", tmpFile, "-dst", "data/subdir/")
	glog.Info(out)
	os.Remove(tmpFile)

	out = runCommandOk(t, "get_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-src", "data/subdir/autoexec.bat", "-dst", dir)
	glog.Info(out)

	got, err := os.ReadFile(tmpFile)
	if err != nil {
		t.Fatalf("error reading fetched file: %s", err)
	}
	if diff := cmp.Diff(expected, got); diff != "" {
		t.Fatalf("unexpected contents of fetched file (-want +got):\n%s", diff)
	}

	// Ensure a second call to prepare resets persistent data
	out = runCommandOk(t, "prepare_fuzzer", "-handle", handle, "-fuzzer", crash_fuzzer)
	glog.Info(out)

	out = runCommandErr(t, "get_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-src", "data/subdir/autoexec.bat", "-dst", dir)
	glog.Info(out)
}

// Test basic fuzzing run
func testFuzzWithoutCorpus(t *testing.T, handle string) {
	crash_fuzzer := "example-fuzzers/crash_fuzzer"

	artifactDir := t.TempDir()
	out := runCommandOk(t, "run_fuzzer", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-artifact-dir", artifactDir, "--", "-artifact_prefix=data/", "-jobs=0",
		"-timeout=25", "-rss_limit_mb=1000")

	glog.Info(out)

	// Only the CFF wrapper echos arguments back
	// TODO(fxbug.dev/106110): Remove this condition after deprecating v1 fuzzers
	if strings.Contains(out, "/pkg/test/crash_fuzzer") {
		if !strings.Contains(out, "-rss_limit_mb=1000") {
			t.Fatalf("rss limit not passed: %s", out)
		}

		if !strings.Contains(out, "-timeout=25") {
			t.Fatalf("timeout not passed: %s", out)
		}
	}

	if !strings.Contains(out, "deadly signal") {
		t.Fatalf("output missing signal: %s", out)
	}

	// This format is emitted by the Go symbolizer, but not the C++ symbolizer
	if !strings.Contains(out, "(anonymous namespace)::crasher") {
		t.Fatalf("stack trace missing expected symbol: %s", out)
	}

	artifactRegex := regexp.MustCompile(`Test unit written to (\S+)`)
	m := artifactRegex.FindStringSubmatch(out)
	if m == nil {
		t.Fatalf("output missing artifact: %s", out)
	}

	artifactPath := m[1]
	if path.Dir(artifactPath) != artifactDir {
		t.Fatalf("artifact path not properly rewritten: %q", artifactPath)
	}

	artifactData, err := os.ReadFile(artifactPath)
	if err != nil {
		t.Fatalf("error reading fetched artifact file: %s", err)
	}
	if !bytes.HasPrefix(artifactData, []byte("HI!")) {
		t.Fatalf("artifact contents unexpected: %q", artifactData)
	}
}

// Test fuzzing run with input corpus
// (Analogous to test_fuzzer_can_boot_and_run_with_corpus in ClusterFuzz)
func testFuzzWithCorpus(t *testing.T, handle string) {
	crash_fuzzer := "example-fuzzers/crash_fuzzer"

	dir := t.TempDir()
	artifactDir := filepath.Join(dir, "artifacts")
	if err := os.Mkdir(artifactDir, 0o700); err != nil {
		t.Fatal(err)
	}
	runCommandOk(t, "prepare_fuzzer", "-handle", handle, "-fuzzer", crash_fuzzer)

	outputCorpus := makeCorpus(t, "new", nil)
	inputElements := []string{"A", "B", "C"}
	inputCorpus := makeCorpus(t, "uninteresting", inputElements)
	runCommandOk(t, "prepare_fuzzer", "-handle", handle, "-fuzzer", crash_fuzzer)
	runCommandOk(t, "put_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-src", outputCorpus, "-dst", "data/corpus")
	runCommandOk(t, "put_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-src", inputCorpus, "-dst", "data/corpus")

	dictionary := filepath.Join(dir, "dictionary")
	if err := os.WriteFile(dictionary, []byte(`"moraine"`), 0o600); err != nil {
		t.Fatalf("error creating dictionary file: %s", err)
	}
	runCommandOk(t, "put_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-src", dictionary, "-dst", "data/")

	// Note: max_total_time is an int flag, but ClusterFuzz sometimes passes floats
	out := runCommandOk(t, "run_fuzzer", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-artifact-dir", artifactDir, "--", "-seed=123", "-artifact_prefix=data/",
		"-jobs=0", "-print_final_stats=1", "-max_total_time=10.0",
		"-dict=data/dictionary", "data/corpus/new", "data/corpus/uninteresting")

	glog.Info(out)

	// ClusterFuzz integration tests expect this to be echoed
	if !strings.Contains(out, "data/corpus/new") {
		t.Fatalf("Output corpus name missing from output:\n%s", out)
	}

	if !strings.Contains(out, "3 files found in") {
		t.Fatalf("Input corpus element count missing from output:\n%s", out)
	}

	if !strings.Contains(out, "Test unit written to") {
		t.Fatalf("Artifact info missing from output:\n%s", out)
	}

	// Emitted by Fuzzer::PrintFinalStats in libFuzzer
	if !strings.Contains(out, "stat::average_exec_per_sec") {
		t.Fatalf("Final stats missing from output:\n%s", out)
	}

	runCommandOk(t, "get_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-dst", outputCorpus, "-src", "data/corpus/new/*")

	outputElements := readCorpus(t, outputCorpus)

	// TODO(fxbug.dev/38760): This is included in the ClusterFuzz tests but may
	// not be a correct assertion. To avoid flake here, a fixed seed is passed above.
	if len(outputElements) <= len(inputElements) {
		t.Fatalf("output corpus smaller than expected: %d", len(outputElements))
	}
}

// Test minimize workflow.
// (Analogous to test_minimize_testcase in ClusterFuzz)
func testMinimize(t *testing.T, handle string) {
	crash_fuzzer := "example-fuzzers/crash_fuzzer"

	artifactDir := t.TempDir()
	runCommandOk(t, "prepare_fuzzer", "-handle", handle, "-fuzzer", crash_fuzzer)

	dir := t.TempDir()
	overlongFile := path.Join(dir, "overlong_crasher")
	overlongContents := []byte("HI!!")
	if err := os.WriteFile(overlongFile, overlongContents, 0o600); err != nil {
		t.Fatalf("error creating overlong testcase file: %s", err)
	}
	runCommandOk(t, "put_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-src", overlongFile, "-dst", "data/")
	os.Remove(overlongFile)

	out := runCommandOk(t, "run_fuzzer", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-artifact-dir", artifactDir, "--", "data/overlong_crasher",
		"-exact_artifact_path=data/final-minimized-crash", "-minimize_crash=1", "-jobs=0",
		"-timeout=25", "-runs=1000000", "-rss_limit_mb=1000")

	glog.Info(out)

	// The output artifact is automatically fetched by `run_fuzzer` above but ClusterFuzz also
	// fetches separately in this case, which should always work.
	runCommandOk(t, "get_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-dst", dir, "-src", "data/final-minimized-crash")

	minimizedFile := filepath.Join(dir, "final-minimized-crash")

	minimizedFileContents, err := os.ReadFile(minimizedFile)
	if err != nil {
		t.Fatalf("error reading minimized testcase: %s", err)
	}
	os.Remove(minimizedFile)

	if !bytes.Equal(minimizedFileContents, []byte("HI!")) {
		t.Fatalf("incorrect minimization result: %q", minimizedFileContents)
	}
}

// Test merge workflow.
func testMerge(t *testing.T, handle string) {
	crash_fuzzer := "example-fuzzers/crash_fuzzer"

	artifactDir := t.TempDir()
	runCommandOk(t, "prepare_fuzzer", "-handle", handle, "-fuzzer", crash_fuzzer)

	mergeCorpus := makeCorpus(t, "merge-corpus", nil)
	inputElements1 := []string{"A", "B", "C"}
	inputCorpus1 := makeCorpus(t, "input1", inputElements1)
	inputElements2 := []string{"H", "B", "HI", "C", "E"}
	inputCorpus2 := makeCorpus(t, "input2", inputElements2)
	runCommandOk(t, "prepare_fuzzer", "-handle", handle, "-fuzzer", crash_fuzzer)
	runCommandOk(t, "put_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-src", mergeCorpus, "-dst", "data/corpus")
	runCommandOk(t, "put_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-src", inputCorpus1, "-dst", "data/corpus")
	runCommandOk(t, "put_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-src", inputCorpus2, "-dst", "data/corpus")

	// Put an empty mergefile
	// Note: mergefiles are not currently used by ClusterFuzz
	dir := t.TempDir()
	mergeFile := filepath.Join(dir, ".mergefile")
	if err := os.WriteFile(mergeFile, nil, 0o600); err != nil {
		t.Fatalf("error creating mergefile: %s", err)
	}
	runCommandOk(t, "put_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-src", mergeFile, "-dst", "data/")
	os.Remove(mergeFile)

	out := runCommandOk(t, "run_fuzzer", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-artifact-dir", artifactDir, "--", "-artifact_prefix=data/", "-jobs=0",
		"data/corpus/merge-corpus", "data/corpus/input1", "data/corpus/input2",
		"-merge=1", "-merge_control_file=data/.mergefile")

	glog.Info(out)

	runCommandOk(t, "get_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-dst", mergeCorpus, "-src", "data/corpus/merge-corpus/*")

	mergeElements := readCorpus(t, mergeCorpus)
	if len(mergeElements) == 0 {
		t.Fatalf("merge corpus was empty")
	}
	if len(mergeElements) >= len(inputElements1)+len(inputElements2) {
		t.Fatalf("merge corpus larger than expected: %d", len(mergeElements))
	}

	// These elements have distinct coverage so should be preserved
	assertSubset(t, []string{"H", "HI"}, mergeElements)

	// We should at least be able to fetch the mergefile, even if it's going to be empty
	runCommandOk(t, "get_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-dst", dir, "-src", "data/.mergefile")
}

// Attempt repro, with ASAN crash
// (Analogous to test_fuzzer_can_boot_and_run_reproducer in ClusterFuzz)
func testReproWithCrash(t *testing.T, handle string) {
	overflow_fuzzer := "example-fuzzers/overflow_fuzzer"

	runCommandOk(t, "prepare_fuzzer", "-handle", handle, "-fuzzer", overflow_fuzzer)

	dir := t.TempDir()
	reproFile := path.Join(dir, "crasher")
	reproFileContents := make([]byte, 12)
	binary.LittleEndian.PutUint64(reproFileContents, 2)
	binary.LittleEndian.PutUint32(reproFileContents[8:], 0x41414141)
	if err := os.WriteFile(reproFile, reproFileContents, 0o600); err != nil {
		t.Fatalf("error creating repro file: %s", err)
	}
	runCommandOk(t, "put_data", "-handle", handle, "-fuzzer", overflow_fuzzer,
		"-src", reproFile, "-dst", "data/")
	os.Remove(reproFile)

	out := runCommandOk(t, "run_fuzzer", "-handle", handle, "-fuzzer",
		overflow_fuzzer, "--", "data/crasher")
	glog.Info(out)

	if !strings.Contains(out, "ERROR: AddressSanitizer: heap-buffer-overflow on address") {
		t.Fatalf("output missing ASAN crash: %s", out)
	}

	// ClusterFuzz integration tests expect this to be echoed
	if !strings.Contains(out, "Running: data/crasher") {
		t.Fatalf("Testcase name missing from output:\n%s", out)
	}
}

// Attempt repro, that shouldn't crash
func testReproWithoutCrash(t *testing.T, handle string) {
	overflow_fuzzer := "example-fuzzers/overflow_fuzzer"

	runCommandOk(t, "prepare_fuzzer", "-handle", handle, "-fuzzer", overflow_fuzzer)

	dir := t.TempDir()
	reproFile := path.Join(dir, "non-crasher")
	reproFileContents := make([]byte, 12)
	binary.LittleEndian.PutUint64(reproFileContents, 8)
	binary.LittleEndian.PutUint32(reproFileContents[8:], 0x41414141)
	if err := os.WriteFile(reproFile, reproFileContents, 0o600); err != nil {
		t.Fatalf("error creating repro file: %s", err)
	}
	runCommandOk(t, "put_data", "-handle", handle, "-fuzzer", overflow_fuzzer,
		"-src", reproFile, "-dst", "data/")
	os.Remove(reproFile)

	out := runCommandOk(t, "run_fuzzer", "-handle", handle, "-fuzzer",
		"example-fuzzers/overflow_fuzzer", "--", "data/non-crasher")
	glog.Info(out)

	// ClusterFuzz integration tests expect this to be echoed
	if !strings.Contains(out, "Running: data/non-crasher") {
		t.Fatalf("Testcase name missing from output:\n%s", out)
	}

	if !strings.Contains(out, "Executed") {
		t.Fatalf("Crash occurred when not expected:\n%s", out)
	}
}

// Test that put/get preserves data
func testPutGet(t *testing.T, handle string) {
	crash_fuzzer := "example-fuzzers/crash_fuzzer"

	dir := t.TempDir()
	testFile := path.Join(dir, "test_file")
	testFileContents := []byte("test file contents!")
	if err := os.WriteFile(testFile, testFileContents, 0o600); err != nil {
		t.Fatalf("error creating test file: %s", err)
	}
	runCommandOk(t, "put_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-src", testFile, "-dst", "data/")
	os.Remove(testFile)

	runCommandOk(t, "get_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-src", "data/test_file", "-dst", dir)

	retrievedContents, err := os.ReadFile(testFile)
	if err != nil {
		t.Fatalf("error reading fetched test file: %s", err)
	}
	if !bytes.Equal(retrievedContents, testFileContents) {
		t.Fatalf("test files do not match: sent '%s', received '%s'",
			testFileContents, retrievedContents)
	}
}

// Test bulk putting a lot of small files, to exercise edge-case conditions.
func testBulkPut(t *testing.T, handle string) {
	crash_fuzzer := "example-fuzzers/crash_fuzzer"

	dir := t.TempDir()
	corpusDir := path.Join(dir, "corpus")
	if err := os.Mkdir(corpusDir, 0o700); err != nil {
		t.Fatal(err)
	}
	for j := 0; j < 2000; j++ {
		corpusPath := path.Join(corpusDir, fmt.Sprintf("corpus-%d", j))
		if err := os.WriteFile(corpusPath, []byte(fmt.Sprintf("%d", j)), 0o600); err != nil {
			t.Fatalf("error creating local corpus file: %s", err)
		}
	}
	runCommandOk(t, "put_data", "-handle", handle, "-fuzzer", crash_fuzzer,
		"-src", corpusDir+"/*", "-dst", "data/corpus")
}

// Covers a case similar to `test_qemu_logs_returned_on_error` in the
// ClusterFuzz integration tests.
func TestGetLogsFromCrashedInstance(t *testing.T) {
	// Override SSH reconnection settings so this test runs faster
	originalInterval := fuzz.DefaultSSHReconnectInterval
	fuzz.DefaultSSHReconnectInterval = 1 * time.Second
	defer func() { fuzz.DefaultSSHReconnectInterval = originalInterval }()

	if _, found := os.LookupEnv("UNDERCOAT_E2E_TESTS"); !found {
		t.Skip("skipping end-to-end test; set UNDERCOAT_E2E_TESTS to enable")
	}

	out := runCommandOk(t, "start_instance")
	handle := strings.TrimSpace(out)

	defer runCommandOk(t, "stop_instance", "-handle", handle)

	crash_fuzzer := "example-fuzzers/crash_fuzzer"

	runCommandOk(t, "run_fuzzer", "-handle", handle, "-fuzzer", crash_fuzzer)

	proc, err := os.FindProcess(getQemuPidFromHandle(t, handle))
	if err != nil {
		t.Fatalf("error finding launcher process: %s", err)
	}
	if err := proc.Kill(); err != nil {
		t.Fatalf("error killing launcher process: %s", err)
	}

	// This should fail, since we just killed the instance
	runCommandErr(t, "run_fuzzer", "-handle", handle, "-fuzzer", crash_fuzzer)

	out = runCommandOk(t, "get_logs", "-handle", handle)
	if !strings.Contains(out, "{{{reset}}}") {
		t.Fatalf("output missing syslog: %s", out)
	}
}

func TestStopInstanceWithOldHandleVersion(t *testing.T) {
	if _, found := os.LookupEnv("UNDERCOAT_E2E_TESTS"); !found {
		t.Skip("skipping end-to-end test; set UNDERCOAT_E2E_TESTS to enable")
	}

	out := runCommandOk(t, "start_instance")
	handle := strings.TrimSpace(out)

	// Keep this around so we can restore it after modifying it, when we want
	// to cleanup
	oldJson, err := os.ReadFile(handle)
	if err != nil {
		runCommandOk(t, "stop_instance", "-handle", handle)
		t.Fatalf("error reading handle: %s", err)
	}

	defer func() {
		if !fileExists(handle) {
			return
		}

		// It wasn't fully stopped, so restore the original file
		if err := os.WriteFile(handle, oldJson, 0o600); err != nil {
			t.Fatalf("error re-writing handle: %s", err)
		}
		runCommandOk(t, "stop_instance", "-handle", handle)
	}()

	pid := getQemuPidFromHandle(t, handle)

	// Put the bare minimum into the handle JSON file
	json := fmt.Sprintf(`{"LauncherType": "QemuLauncher", "Launcher": {"Pid": %d}}`, pid)
	if err := os.WriteFile(handle, []byte(json), 0o600); err != nil {
		t.Fatalf("error writing handle: %s", err)
	}

	runCommandOk(t, "stop_instance", "-handle", handle)
}

// Test suppressing the stdout and syslog of a noisy fuzzer.
func TestFuzzNoisy(t *testing.T) {
	if _, found := os.LookupEnv("UNDERCOAT_E2E_TESTS"); !found {
		t.Skip("skipping end-to-end test; set UNDERCOAT_E2E_TESTS to enable")
	}

	out := runCommandOk(t, "start_instance")
	handle := strings.TrimSpace(out)

	defer runCommandOk(t, "stop_instance", "-handle", handle)

	noisy_fuzzer := "undercoat-test-fuzzers/noisy_fuzzer"
	out = runCommandOk(t, "prepare_fuzzer", "-handle", handle, "-fuzzer", noisy_fuzzer)
	glog.Info(out)

	out = runCommandOk(t, "run_fuzzer", "-handle", handle, "-fuzzer", noisy_fuzzer, "--",
		"-max_total_time=1")
	glog.Info(out)

	if !strings.Contains(out, "libFuzzer starting") {
		t.Fatalf("output missing stderr: %s", out)
	}

	if strings.Contains(out, "stdout-noise") {
		t.Fatalf("output includes stdout: %s", out)
	}

	if strings.Contains(out, "syslog-noise") {
		t.Fatalf("output includes syslog: %s", out)
	}
}

// Helper functions:

// Grab the pid for QEMU right out the handle file.
// This relies on knowing the internal implementation details, but has fewer
// unwanted side effects than the "killall qemu" approach used by ClusterFuzz.
func getQemuPidFromHandle(t *testing.T, handle string) int {
	data, err := os.ReadFile(handle)
	if err != nil {
		t.Fatalf("error reading handle: %s", err)
	}

	type launcherSkel struct {
		Pid int
	}
	type handleSkel struct {
		Launcher launcherSkel
	}
	var handleData handleSkel
	if err := json.Unmarshal(data, &handleData); err != nil {
		t.Fatalf("error deserializing handle: %s", err)
	}

	return handleData.Launcher.Pid
}

// Runs a command that is expected to succeed, and returns stdout
func runCommandOk(t *testing.T, args ...string) string {
	return runCommandImpl(t, true /* shouldSucceed */, args...)
}

// Runs a command that is expected to fail, and returns stdout
func runCommandErr(t *testing.T, args ...string) string {
	return runCommandImpl(t, false /* shouldSucceed */, args...)
}

// Do not call directly; use either runCommandOk or runCommandErr above.
func runCommandImpl(t *testing.T, shouldSucceed bool, args ...string) string {
	// Skip frames so we can get useful information about the line that failed,
	// and not just this function every time.
	_, file, line, _ := runtime.Caller(2)

	cmd, err := fuzz.ParseArgs(args)
	if err != nil {
		t.Fatalf("Error parsing args (%s:%d): %s", file, line, err)
	}

	var buf bytes.Buffer
	if err := cmd.Execute(&buf); (err == nil) != shouldSucceed {
		t.Fatalf("Unexpected result executing command (%s:%d): %s", file, line, err)
	}

	return buf.String()
}

// Makes a corpus dir with the specified input elements and returns the directory path.
func makeCorpus(t *testing.T, name string, inputs []string) string {
	dir := filepath.Join(t.TempDir(), name)
	if err := os.Mkdir(dir, 0o700); err != nil {
		t.Fatal(err)
	}
	for _, content := range inputs {
		corpusPath := path.Join(dir, "corpus-"+content)
		if err := os.WriteFile(corpusPath, []byte(content), 0o600); err != nil {
			t.Fatalf("error creating input corpus file: %s", err)
		}
	}
	return dir
}

// Reads a local corpus dir and returns its contents as a list of strings (one per file).
func readCorpus(t *testing.T, path string) []string {
	entries, err := os.ReadDir(path)
	if err != nil {
		t.Fatalf("error enumerating corpus %q: %s", path, err)
	}
	glog.Infof("Contents of corpus in %q:", path)
	var elements []string
	for j, entry := range entries {
		contents, err := os.ReadFile(filepath.Join(path, entry.Name()))
		if err != nil {
			t.Fatalf("error reading corpus element: %s", err)
		}
		glog.Infof("  - #%d (%s): %q", j, entry.Name(), contents)
		elements = append(elements, string(contents))
	}

	return elements
}

// Asserts that corpusA is a subset of corpusB.
func assertSubset(t *testing.T, corpusA []string, corpusB []string) {
	corpusBElements := make(map[string]bool)

	for _, el := range corpusB {
		corpusBElements[el] = true
	}

	for _, el := range corpusA {
		if !corpusBElements[el] {
			t.Fatalf("corpus missing expected element %q", el)
		}
	}
}

func fileExists(path string) bool {
	_, err := os.Stat(path)
	return !os.IsNotExist(err)
}
