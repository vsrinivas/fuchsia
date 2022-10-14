// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"bytes"
	"fmt"
	"path/filepath"
	"reflect"
	"strings"
	"testing"

	"github.com/golang/glog"
	"github.com/google/go-cmp/cmp"
)

func TestIsExample(t *testing.T) {
	build, _ := newMockBuild()
	f, err := build.Fuzzer("foo/bar")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}
	if f.IsExample() {
		t.Fatalf("%s marked as example but shouldn't be", f.Name)
	}

	f, err = build.Fuzzer("example-fuzzers/noop_fuzzer")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}
	if !f.IsExample() {
		t.Fatalf("%s not marked as example but should be", f.Name)
	}
}

func TestIsV2(t *testing.T) {
	build, _ := newMockBuild()
	f, err := build.Fuzzer("foo/bar")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}
	if f.isV2() {
		t.Fatalf("incorrect version detection for fuzzer %s", f.Name)
	}

	f, err = build.Fuzzer("cff/fuzzer")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}
	if !f.isV2() {
		t.Fatalf("incorrect version detection for fuzzer %s", f.Name)
	}
}

func TestUseFuzzCtl(t *testing.T) {
	build, _ := newMockBuild()
	f, err := build.Fuzzer("cff/fuzzer")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}
	if !f.useFuzzCtl() {
		t.Fatalf("fuzz_ctl incorrectly ignored by fuzzer %s", f.Name)
	}

	build.(*mockBuild).useFfxFuzz = true
	f, err = build.Fuzzer("cff/fuzzer")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}
	if f.useFuzzCtl() {
		t.Fatalf("fuzz_ctl incorrectly used by fuzzer %s", f.Name)
	}
}

func TestUseFfxFuzz(t *testing.T) {
	build, _ := newMockBuild()
	f, err := build.Fuzzer("cff/fuzzer")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}
	if f.useFfxFuzz() {
		t.Fatalf("ffx fuzz incorrectly used by fuzzer %s", f.Name)
	}

	build.(*mockBuild).useFfxFuzz = true
	f, err = build.Fuzzer("cff/fuzzer")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}
	if !f.useFfxFuzz() {
		t.Fatalf("ffx fuzz incorrectly ignored by fuzzer %s", f.Name)
	}
}

func TestAbsPath(t *testing.T) {
	build, _ := newMockBuild()

	f, err := build.Fuzzer("foo/bar")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}

	absPaths := map[string]string{
		"pkg/data/relpath":  "/pkgfs/packages/foo/0/data/relpath",
		"/pkg/data/relpath": "/pkg/data/relpath",
		"data/relpath":      "/tmp/r/sys/fuchsia.com:foo:0#meta:bar.cmx/relpath",
		"/data/relpath":     "/data/relpath",
		"relpath":           "/relpath",
		"/relpath":          "/relpath",
	}
	for relpath, expected := range absPaths {
		got := f.AbsPath(relpath)
		if expected != got {
			t.Fatalf("expected %q, got %q", expected, got)
		}
	}
}

func TestAbsPathUsingFuzzCtl(t *testing.T) {
	build, _ := newMockBuild()

	f, err := build.Fuzzer("cff/fuzzer")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}

	absPaths := map[string]string{
		"pkg/data/relpath":  "/tmp/fuzz_ctl/fuchsia.com/cff/fuzzer/pkg/data/relpath",
		"/pkg/data/relpath": "/pkg/data/relpath",
		"data/relpath":      "/tmp/fuzz_ctl/fuchsia.com/cff/fuzzer/relpath",
		"/data/relpath":     "/data/relpath",
		"relpath":           "/tmp/fuzz_ctl/fuchsia.com/cff/fuzzer/relpath",
		"/relpath":          "/relpath",
	}

	for relpath, expected := range absPaths {
		got := f.AbsPath(relpath)
		if expected != got {
			t.Fatalf("expected %q, got %q", expected, got)
		}
	}
}

func TestAbsPathUsingFfxFuzz(t *testing.T) {
	build, _ := newMockBuild()
	build.(*mockBuild).useFfxFuzz = true

	f, err := build.Fuzzer("cff/fuzzer")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}

	cachePath := "CACHE:fuchsia-pkg:__fuchsia.com_cff__meta_fuzzer.cm/"
	absPaths := map[string]string{
		"some/path":  cachePath + "some/path",
		"/some/path": cachePath + "some/path",
		cachePath:    cachePath,
	}
	for relpath, expected := range absPaths {
		got := f.AbsPath(relpath)
		if expected != got {
			t.Fatalf("expected %q, got %q", expected, got)
		}
	}
}

func TestParse(t *testing.T) {
	build, _ := newMockBuild()
	f, err := build.Fuzzer("foo/bar")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}

	f.Parse([]string{"arg", "-k1=v1", "-k1=v2", "-k2=v-3", "-bad", "--alsobad", "-k3=has=two"})
	if !reflect.DeepEqual(f.args, []string{"arg", "-bad", "--alsobad"}) {
		t.Fatalf("missing arg(s): %s", strings.Join(f.args, " "))
	}
	if k1, found := f.options["k1"]; !found || k1 != "v2" {
		t.Fatalf("expected v2, got %s", k1)
	}
	if k2, found := f.options["k2"]; !found || k2 != "v-3" {
		t.Fatalf("expected v-3, got %s", k2)
	}
	if k3, found := f.options["k3"]; !found || k3 != "has=two" {
		t.Fatalf("expected has=two, got %s", k3)
	}
}

func TestParseArgsForFfx(t *testing.T) {
	build, _ := newMockBuild()
	build.(*mockBuild).useFfxFuzz = true
	conn := NewMockConnector(t)

	f, err := build.Fuzzer("cff/fuzzer")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}

	conn.Dirs = []string{f.AbsPath("some/dir"), f.AbsPath("out"),
		f.AbsPath("in1"), f.AbsPath("in2")}

	// Args that are expected to raise errors when parsing

	errorArgs := [][]string{
		{"some/dir", "some/file"},
		{"some/file", "some/dir"},
		{invalidPath},
		{"-merge=1", "-minimize_crash=1"},
		{"-minimize_crash=1"},
		{"-minimize_crash=1", "some/dir"},
		{"-merge=1", "some/file"},
		{"-merge=1", "some/dir"},
	}

	for _, args := range errorArgs {
		f.Parse(args)

		if _, err := f.parseArgsForFfx(conn); err == nil {
			t.Fatalf("unexpectedly succeeded parsing args %q for CFF", args)
		}
	}

	// Args that should succeed
	//
	// Note: It's slightly harder to read, but we use two parallel arrays here
	// because maps can't use slices as keys.
	testArgs := [][]string{
		{},
		{"some/file"},
		{"some/dir"},
		{"out", "in1", "in2"},
		{"-merge=1", "out", "in1", "in2"},
		{"-minimize_crash=1", "some/file"},
	}

	expectedBaseArgs := []string{f.url, "--no-stdout", "--no-syslog"}
	expectedConfigs := []*ffxFuzzRunConfig{
		{
			command: "run",
			args:    expectedBaseArgs,
		},
		{
			command:      "try",
			args:         append(expectedBaseArgs, f.AbsPath("some/file")),
			testcasePath: "some/file",
		},
		{
			command:      "run",
			args:         expectedBaseArgs,
			outputCorpus: "some/dir",
		},
		{
			command:      "run",
			args:         expectedBaseArgs,
			outputCorpus: "out",
			inputCorpora: []string{"in1", "in2"},
		},
		{
			command:      "merge",
			args:         expectedBaseArgs,
			outputCorpus: "out",
			inputCorpora: []string{"in1", "in2"},
		},
		{
			command:      "minimize",
			args:         append(expectedBaseArgs, f.AbsPath("some/file")),
			testcasePath: "some/file",
		},
	}

	for j, args := range testArgs {
		f.Parse(args)

		got, err := f.parseArgsForFfx(conn)
		if err != nil {
			t.Fatalf("failed to parse args %q for CFF: %s", args, err)
		}

		expected := expectedConfigs[j]
		if diff := cmp.Diff(expected, got, cmp.AllowUnexported(ffxFuzzRunConfig{})); diff != "" {
			t.Fatalf("ffx config for args %q not as expected (-want +got):\n%s", args, diff)
		}
	}
}

func TestPrepare(t *testing.T) {
	build, _ := newMockBuild()
	f, err := build.Fuzzer("foo/bar")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}

	conn := NewMockConnector(t)
	conn.connected = true
	if err := f.Prepare(conn); err != nil {
		t.Fatalf("failed to prepare fuzzer: %s", err)
	}

	if len(conn.FfxHistory) != 0 {
		t.Fatalf("incorrect ffx history: %v", conn.FfxHistory)
	}

	if !reflect.DeepEqual(conn.CmdHistory, []string{"pkgctl", "killall", "rm"}) {
		t.Fatalf("incorrect command history: %v", conn.CmdHistory)
	}
}

func TestPrepareUsingFuzzCtl(t *testing.T) {
	build, _ := newMockBuild()
	f, err := build.Fuzzer("cff/fuzzer")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}

	conn := NewMockConnector(t)
	conn.connected = true
	if err := f.Prepare(conn); err != nil {
		t.Fatalf("failed to prepare fuzzer: %s", err)
	}

	if !reflect.DeepEqual(conn.CmdHistory, []string{"fuzz_ctl"}) {
		t.Fatalf("incorrect command history: %v", conn.CmdHistory)
	}

	expected := []string{"reset fuchsia-pkg://fuchsia.com/cff#meta/fuzzer.cm"}
	if diff := cmp.Diff(expected, conn.FuzzCtlHistory); diff != "" {
		t.Fatalf("incorrect fuzz_ctl history (-want +got):\n%s", diff)
	}

	if len(conn.FfxHistory) != 0 {
		t.Fatalf("incorrect ffx history: %v", conn.FfxHistory)
	}
}

func TestPrepareUsingFfxFuzz(t *testing.T) {
	build, _ := newMockBuild()
	build.(*mockBuild).useFfxFuzz = true
	f, err := build.Fuzzer("cff/fuzzer")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}

	conn := NewMockConnector(t)
	conn.connected = true
	if err := f.Prepare(conn); err != nil {
		t.Fatalf("failed to prepare fuzzer: %s", err)
	}

	// MockConnector calls `rm`
	if !reflect.DeepEqual(conn.CmdHistory, []string{"rm"}) {
		t.Fatalf("incorrect command history: %v", conn.CmdHistory)
	}

	if len(conn.FuzzCtlHistory) != 0 {
		t.Fatalf("incorrect fuzz_ctl history: %v", conn.FuzzCtlHistory)
	}

	expected := []string{"fuzz stop fuchsia-pkg://fuchsia.com/cff#meta/fuzzer.cm"}
	if diff := cmp.Diff(expected, conn.FfxHistory); diff != "" {
		t.Fatalf("incorrect ffx history (-want +got):\n%s", diff)
	}
}

func TestSetOptionsUsingFfxFuzz(t *testing.T) {
	build, _ := newMockBuild()
	build.(*mockBuild).useFfxFuzz = true
	conn := NewMockConnector(t)

	f, err := build.Fuzzer("cff/fuzzer")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}

	// All valid options
	args := []string{"-runs=3", "-max_total_time=10", "-malloc_limit_mb=42",
		"-artifact_prefix=data/", "-seed=123", "-jobs=0", "-max_len=3141",
		"-print_final_stats=1"}
	f.Parse(args)

	if err := f.setCFFOptions(conn); err != nil {
		t.Fatalf("failed to set CFF options: %s", err)
	}

	expected := []string{
		"fuzz set fuchsia-pkg://fuchsia.com/cff#meta/fuzzer.cm runs 3",
		"fuzz set fuchsia-pkg://fuchsia.com/cff#meta/fuzzer.cm max_total_time 10s",
		"fuzz set fuchsia-pkg://fuchsia.com/cff#meta/fuzzer.cm seed 123",
		"fuzz set fuchsia-pkg://fuchsia.com/cff#meta/fuzzer.cm max_input_size 3141b",
		"fuzz set fuchsia-pkg://fuchsia.com/cff#meta/fuzzer.cm mutation_depth 5",
		"fuzz set fuchsia-pkg://fuchsia.com/cff#meta/fuzzer.cm detect_leaks false",
		"fuzz set fuchsia-pkg://fuchsia.com/cff#meta/fuzzer.cm run_limit 1200s",
		"fuzz set fuchsia-pkg://fuchsia.com/cff#meta/fuzzer.cm malloc_limit 42mb",
		"fuzz set fuchsia-pkg://fuchsia.com/cff#meta/fuzzer.cm oom_limit 2gb",
		"fuzz set fuchsia-pkg://fuchsia.com/cff#meta/fuzzer.cm purge_interval 1s",
		"fuzz set fuchsia-pkg://fuchsia.com/cff#meta/fuzzer.cm print_final_stats 1",
		"fuzz set fuchsia-pkg://fuchsia.com/cff#meta/fuzzer.cm use_value_profile false",
	}

	if diff := cmp.Diff(expected, conn.FfxHistory, sortSlicesOpt); diff != "" {
		t.Fatalf("ffx fuzz set commands not as expected (-want +got):\n%s", diff)
	}

	// Invalid option

	args = []string{"-libfuzzer_feature=unsupported"}
	f.Parse(args)

	if err := f.setCFFOptions(conn); err == nil {
		t.Fatalf("expected error for unsupported libfuzzer feature")
	}

	args = []string{"-jobs=1"}
	f.Parse(args)

	if err := f.setCFFOptions(conn); err == nil {
		t.Fatalf("expected error for jobs != 0")
	}
}

func TestMarkOutputCorpusForFfx(t *testing.T) {
	build, _ := newMockBuild()
	build.(*mockBuild).useFfxFuzz = true
	conn := NewMockConnector(t)

	f, err := build.Fuzzer("cff/fuzzer")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}

	corpusPath := "some/corpus"
	f.markOutputCorpus(conn, corpusPath)

	if len(conn.PathsPut) != 1 {
		t.Fatalf("no Put was made")
	}
	putCmd := conn.PathsPut[0]
	if putCmd.dst != f.AbsPath(corpusPath) {
		t.Fatalf("Put was to unexpected destination: %s", putCmd.dst)
	}

	if filepath.Base(putCmd.src) != liveCorpusMarkerName {
		t.Fatalf("Put was with wrong filename: %s", putCmd.src)
	}

	if diff := cmp.Diff(f.url, conn.LastPutFileContent); diff != "" {
		t.Fatalf("corpus marker has wrong contents (-want +got):\n%s", diff)
	}
}

// Construct and run fuzzer and collect its output and artifacts using a default `mockBuild`.
func runWithDefaults(t *testing.T, name string, args []string) (string, []string, error) {
	build, _ := newMockBuild()
	return runWithBuild(t, build, name, args)
}

// Construct and run fuzzer and collect its output and artifacts using a given `build`.
func runWithBuild(t *testing.T, build Build, name string, args []string) (string, []string, error) {
	conn := NewMockConnector(t)
	return runWithConnector(t, build, conn, name, args)
}

// Construct and run fuzzer and collect its output and artifacts using a given `mockConnector`.
func runWithConnector(t *testing.T, build Build, conn *mockConnector, name string, args []string) (string, []string, error) {
	f, err := build.Fuzzer(name)
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}
	return runWithFuzzer(t, conn, f, args)
}

// Run a given `Fuzzer` and collect its output and artifacts.
func runWithFuzzer(t *testing.T, conn *mockConnector, f *Fuzzer, args []string) (string, []string, error) {
	f.Parse(append(args, "data/corpus"))

	var outBuf bytes.Buffer
	artifacts, err := f.Run(conn, &outBuf, "/some/artifactDir")

	return outBuf.String(), artifacts, err
}

func testRun(t *testing.T, name string) {
	build, _ := newMockBuild()
	conn := NewMockConnector(t)
	f, err := build.Fuzzer(name)
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}

	out, artifacts, err := runWithFuzzer(t, conn, f, []string{"-merge_control_file=data/.mergefile"})
	if err != nil {
		t.Fatalf("failed to run fuzzer: %s", err)
	}

	// Check for syslog insertion
	if !strings.Contains(out, fmt.Sprintf("syslog for %d", mockFuzzerPid)) {
		t.Fatalf("fuzzer output missing syslog: %q", out)
	}

	// Check for symbolization
	if !strings.Contains(out, "wow.c:1") {
		t.Fatalf("fuzzer output not properly symbolized: %q", out)
	}

	// Check for artifact detection
	artifactAbsPath := f.AbsPath("tmp/crash-1312")
	if !reflect.DeepEqual(artifacts, []string{artifactAbsPath}) {
		t.Fatalf("unexpected artifact list: %s", artifacts)
	}

	// Check for artifact path rewriting
	if !strings.Contains(out, "/some/artifactDir/crash-1312") {
		t.Fatalf("artifact prefix not rewritten: %q", out)
	}

	// Check that paths in libFuzzer options/args are translated
	if !f.useFuzzCtl() && !strings.Contains(out, "tmp/.mergefile") {
		t.Fatalf("mergefile prefix not rewritten: %q", out)
	}

	if !f.useFuzzCtl() && !strings.Contains(out, "tmp/corpus") {
		t.Fatalf("corpus prefix not rewritten: %q", out)
	}

	if !strings.Contains(out, "\nRunning: data/corpus/testcase\n") {
		t.Fatalf("testcase prefix not restored: %q", out)
	}
}

func TestRun(t *testing.T) {
	testRun(t, "foo/bar")
}

func TestRunUsingFuzzCtl(t *testing.T) {
	testRun(t, "cff/fuzzer")
}

func TestRunWithInvalidArtifactPrefix(t *testing.T) {
	_, _, err := runWithDefaults(t, "foo/bar", []string{"-artifact_prefix=foo/bar"})
	if err == nil || !strings.Contains(err.Error(), "artifact_prefix not in mutable") {
		t.Fatalf("expected failure to run but got: %s", err)
	}
}

func TestRunWithFuzzerErrorUsingFuzzCtl(t *testing.T) {
	build, _ := newMockBuild()

	_, _, err := runWithBuild(t, build, "cff/broken", nil)
	if err == nil || !strings.Contains(err.Error(), "internal error") {
		t.Fatalf("expected failure to run but got: %s", err)
	}
}

func TestRunWithFuzzerErrorUsingFfxFuzz(t *testing.T) {
	build, _ := newMockBuild()
	build.(*mockBuild).useFfxFuzz = true

	f, err := build.Fuzzer("cff/broken")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}
	conn := NewMockConnector(t)
	conn.Dirs = []string{f.AbsPath("data/corpus")}

	_, _, err = runWithConnector(t, build, conn, "cff/broken", nil)

	// TODO(fxbug.dev/112048): Validate the specific expected error.
	if err == nil {
		t.Fatalf("expected failure to run but got: %s", err)
	}
}

func TestMissingPID(t *testing.T) {
	output, _, err := runWithDefaults(t, "fail/nopid", nil)
	if err != nil {
		t.Fatalf("expected to succeed but got: %s", err)
	}

	if !strings.Contains(output, "missing pid") {
		t.Fatalf("expected missing pid but got: %q", output)
	}
}

func TestSyslogFailure(t *testing.T) {
	build, _ := newMockBuild()
	conn := NewMockConnector(t)
	conn.shouldFailToGetSysLog = true
	output, _, err := runWithConnector(t, build, conn, "foo/bar", nil)
	if err != nil {
		t.Fatalf("expected to succeed but got: %s", err)
	}

	if !strings.Contains(output, "failed to fetch syslog") {
		t.Fatalf("expected syslog fetch failure but got: %q", output)
	}
}

func TestMissingSymbolizer(t *testing.T) {
	build, _ := newMockBuild()
	build.(*mockBuild).brokenSymbolizer = true
	_, _, err := runWithBuild(t, build, "foo/bar", nil)
	if err == nil || !strings.Contains(err.Error(), "failed during symbolization") {
		t.Fatalf("expected failure to symbolize but got: %s", err)
	}
}

func TestMissingFuzzerPackage(t *testing.T) {
	_, _, err := runWithDefaults(t, "fail/notfound", nil)
	if err == nil || !strings.Contains(err.Error(), "not found") {
		t.Fatalf("expected failure to find package but got: %s", err)
	}
}

func runFuzzerV2(t *testing.T, conn *mockConnector, fuzzer *Fuzzer,
	args []string) (string, []string) {

	conn.Dirs = []string{fuzzer.AbsPath("data/corpus/out"),
		fuzzer.AbsPath("data/corpus/in1"),
		fuzzer.AbsPath("data/corpus/in2"),
	}

	fuzzer.Parse(args)

	var outBuf bytes.Buffer
	artifacts, err := fuzzer.Run(conn, &outBuf, "/some/artifactDir")

	out := outBuf.String()

	if err != nil {
		glog.Warningf("fuzzer output: %s", out)
		t.Fatalf("failed to run fuzzer: %s", err)
	}

	return out, artifacts
}

func expectLiveCorpusContents(t *testing.T, conn *mockConnector, targetPaths []string) {
	var added []string
	prefix := "fuzz add fuchsia-pkg://fuchsia.com/cff#meta/fuzzer.cm "
	for _, cmd := range conn.FfxHistory {
		if strings.HasPrefix(cmd, prefix) {
			added = append(added, strings.TrimPrefix(cmd, prefix))
		}
	}
	if diff := cmp.Diff(targetPaths, added, sortSlicesOpt); diff != "" {
		t.Fatalf("incorrect live corpus contents (-want +got):\n%s", diff)
	}
}

func TestFuzzUsingFfxFuzz(t *testing.T) {
	build, _ := newMockBuild()
	build.(*mockBuild).useFfxFuzz = true

	f, err := build.Fuzzer("cff/fuzzer")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}
	conn := NewMockConnector(t)
	out, artifacts := runFuzzerV2(t, conn, f, []string{"data/corpus/out",
		"data/corpus/in1", "data/corpus/in2"})

	// Check that fuzz was run
	if !strings.Contains(out, "Running fuzzer") {
		t.Fatalf("fuzz not run: %q", out)
	}

	// Check that input corpora were added to the live corpus
	expectLiveCorpusContents(t, conn, []string{
		f.AbsPath("data/corpus/in1"), f.AbsPath("data/corpus/in2")})

	// Check that output corpus was marked
	markerPath := filepath.Join("data/corpus/out", liveCorpusMarkerName)
	if !conn.TargetPathExists(f.AbsPath(markerPath)) {
		t.Fatalf("output corpus not marked")
	}

	// Check for syslog insertion
	if !strings.Contains(out, fmt.Sprintf("syslog for %d", mockFuzzerPid)) {
		t.Fatalf("fuzzer output missing syslog: %q", out)
	}

	// Check for symbolization
	if !strings.Contains(out, "wow.c:1") {
		t.Fatalf("fuzzer output not properly symbolized: %q", out)
	}

	// Check for artifact detection
	artifactAbsPath := f.AbsPath("tmp/crash-1312")
	if !reflect.DeepEqual(artifacts, []string{artifactAbsPath}) {
		t.Fatalf("unexpected artifact list: %s", artifacts)
	}

	// Check for artifact path rewriting, with libFuzzer style output
	if !strings.Contains(out, "Test unit written to /some/artifactDir/crash-1312") {
		t.Fatalf("artifact prefix not rewritten: %q", out)
	}

	if !strings.Contains(out, "data/corpus/out") {
		t.Fatalf("output corpus not rewritten: %q", out)
	}
}

func TestTryUsingFfxFuzz(t *testing.T) {
	build, _ := newMockBuild()
	build.(*mockBuild).useFfxFuzz = true

	f, err := build.Fuzzer("cff/fuzzer")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}
	conn := NewMockConnector(t)

	testcase := filepath.Join(t.TempDir(), "testcase")
	touchRandomFile(t, testcase)
	if err := conn.Put(testcase, f.AbsPath("data/")); err != nil {
		t.Fatalf("error putting testcase: %s", err)
	}

	out, artifacts := runFuzzerV2(t, conn, f, []string{"data/testcase"})

	// Check that try was run
	if !strings.Contains(out, "Trying an input") {
		t.Fatalf("try not run: %q", out)
	}

	// Check for syslog insertion
	if !strings.Contains(out, fmt.Sprintf("syslog for %d", mockFuzzerPid)) {
		t.Fatalf("fuzzer output missing syslog: %q", out)
	}

	// Check for symbolization
	if !strings.Contains(out, "wow.c:1") {
		t.Fatalf("fuzzer output not properly symbolized: %q", out)
	}

	// Should have no artifacts generated
	if len(artifacts) > 0 {
		t.Fatalf("artifacts unexpectedly returned: %q", artifacts)
	}

	if !strings.Contains(out, "\nRunning: data/testcase\n") {
		t.Fatalf("testcase name not restored: %q", out)
	}
}

func TestMinimizeUsingFfxFuzz(t *testing.T) {
	build, _ := newMockBuild()
	build.(*mockBuild).useFfxFuzz = true

	f, err := build.Fuzzer("cff/fuzzer")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}
	conn := NewMockConnector(t)

	testcase := filepath.Join(t.TempDir(), "testcase")
	touchRandomFile(t, testcase)
	if err := conn.Put(testcase, f.AbsPath("data/")); err != nil {
		t.Fatalf("error putting testcase: %s", err)
	}

	out, artifacts := runFuzzerV2(t, conn, f, []string{"-minimize_crash=1",
		"-exact_artifact_path=data/final-minimized-crash", "data/testcase"})

	// Check that minimization was run
	if !strings.Contains(out, "Attempting to minimize") {
		t.Fatalf("minimization not run: %q", out)
	}

	// Should have exactly one artifact
	if len(artifacts) != 1 {
		t.Fatalf("unexpected number of artifacts returned (%d)", len(artifacts))
	}

	// Should have been copied into the location specified by exact_artifact_prefix
	if !conn.TargetPathExists(f.AbsPath("data/final-minimized-crash")) {
		t.Fatalf("artifact not moved according to exact_artifact_prefix")
	}
}

func TestMergeUsingFfxFuzz(t *testing.T) {
	build, _ := newMockBuild()
	build.(*mockBuild).useFfxFuzz = true

	f, err := build.Fuzzer("cff/fuzzer")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}
	conn := NewMockConnector(t)

	out, artifacts := runFuzzerV2(t, conn, f, []string{"-merge=1",
		"-merge_control_file=data/.mergefile", "data/corpus/out",
		"data/corpus/in1", "data/corpus/in2"})

	// Check that merge was run
	if !strings.Contains(out, "Compacting fuzzer corpus") {
		t.Fatalf("merge not run: %q", out)
	}

	// Check that input corpora were added to the live corpus
	expectLiveCorpusContents(t, conn, []string{
		f.AbsPath("data/corpus/in1"), f.AbsPath("data/corpus/in2")})

	// Should have no artifacts
	if len(artifacts) != 0 {
		t.Fatalf("unexpected number of artifacts returned (%d)", len(artifacts))
	}

	// Check that output corpus was not marked
	markerPath := filepath.Join("data/corpus/out", liveCorpusMarkerName)
	if conn.TargetPathExists(f.AbsPath(markerPath)) {
		t.Fatalf("output corpus was marked but should not have been")
	}

	// Check that output corpus has output elements
	corpusA := f.AbsPath(filepath.Join("data/corpus/out", "a"))
	corpusB := f.AbsPath(filepath.Join("data/corpus/out", "b"))
	if !conn.TargetPathExists(corpusA) || !conn.TargetPathExists(corpusB) {
		t.Fatalf("merge output corpus not put into expected place")
	}
}
