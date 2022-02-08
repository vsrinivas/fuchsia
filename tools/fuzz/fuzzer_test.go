// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"bytes"
	"reflect"
	"strings"
	"testing"
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

func TestPrepare(t *testing.T) {
	build, _ := newMockBuild()
	f, err := build.Fuzzer("foo/bar")
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}

	conn := &mockConnector{connected: true}
	if err := f.Prepare(conn); err != nil {
		t.Fatalf("failed to prepare fuzzer: %s", err)
	}

	if !reflect.DeepEqual(conn.CmdHistory, []string{"pkgctl", "killall", "rm"}) {
		t.Fatalf("incorrect command history: %v", conn.CmdHistory)
	}
}

const (
	FuzzerNormal = iota
	FuzzerSymbolizerFailure
	FuzzerSyslogFailure
)

// Run fuzzer and collect its output and artifacts. Scenario should be one of
// those listed above.
func runFuzzer(t *testing.T, name string, args []string, scenario int) (string, []string, error) {
	build, _ := newMockBuild()
	conn := &mockConnector{}

	switch scenario {
	case FuzzerSymbolizerFailure:
		build.(*mockBuild).brokenSymbolizer = true
	case FuzzerSyslogFailure:
		conn.shouldFailToGetSysLog = true
	}

	f, err := build.Fuzzer(name)
	if err != nil {
		t.Fatalf("failed to load fuzzer: %s", err)
	}

	f.Parse(append(args, "data/corpus"))

	var outBuf bytes.Buffer
	artifacts, err := f.Run(conn, &outBuf, "/some/artifactDir")

	return outBuf.String(), artifacts, err
}

func TestRun(t *testing.T) {
	out, artifacts, err := runFuzzer(t, "foo/bar",
		[]string{"-merge_control_file=data/.mergefile"}, FuzzerNormal)
	if err != nil {
		t.Fatalf("failed to run fuzzer: %s", err)
	}

	// Check for syslog insertion
	if !strings.Contains(out, "syslog for 123") {
		t.Fatalf("fuzzer output missing syslog: %q", out)
	}

	// Check for symbolization
	if !strings.Contains(out, "wow.c:1") {
		t.Fatalf("fuzzer output not properly symbolized: %q", out)
	}

	// Check for artifact detection
	artifactAbsPath := "/tmp/r/sys/fuchsia.com:foo:0#meta:bar.cmx/crash-1312"
	if !reflect.DeepEqual(artifacts, []string{artifactAbsPath}) {
		t.Fatalf("unexpected artifact list: %s", artifacts)
	}

	// Check for artifact path rewriting
	if !strings.Contains(out, "/some/artifactDir/crash-1312") {
		t.Fatalf("artifact prefix not rewritten: %q", out)
	}

	// Check that paths in libFuzzer options/args are translated
	if !strings.Contains(out, "tmp/.mergefile") {
		t.Fatalf("mergefile prefix not rewritten: %q", out)
	}

	if !strings.Contains(out, "tmp/corpus") {
		t.Fatalf("corpus prefix not rewritten: %q", out)
	}

	if !strings.Contains(out, "\nRunning: data/corpus/testcase\n") {
		t.Fatalf("testcase prefix not restored: %q", out)
	}
}

func TestRunWithInvalidArtifactPrefix(t *testing.T) {
	args := []string{"-artifact_prefix=foo/bar"}
	_, _, err := runFuzzer(t, "foo/bar", args, FuzzerNormal)
	if err == nil || !strings.Contains(err.Error(), "artifact_prefix not in mutable") {
		t.Fatalf("expected failure to run but got: %s", err)
	}
}

func TestMissingPID(t *testing.T) {
	output, _, err := runFuzzer(t, "fail/nopid", nil, FuzzerNormal)

	if err != nil {
		t.Fatalf("expected to succeed but got: %s", err)
	}

	if !strings.Contains(output, "missing pid") {
		t.Fatalf("expected missing pid but got: %q", output)
	}
}

func TestSyslogFailure(t *testing.T) {
	output, _, err := runFuzzer(t, "foo/bar", nil, FuzzerSyslogFailure)

	if err != nil {
		t.Fatalf("expected to succeed but got: %s", err)
	}

	if !strings.Contains(output, "failed to fetch syslog") {
		t.Fatalf("expected syslog fetch failure but got: %q", output)
	}
}

func TestMissingSymbolizer(t *testing.T) {
	_, _, err := runFuzzer(t, "foo/bar", nil, FuzzerSymbolizerFailure)
	if err == nil || !strings.Contains(err.Error(), "failed during symbolization") {
		t.Fatalf("expected failure to symbolize but got: %s", err)
	}
}

func TestMissingFuzzerPackage(t *testing.T) {
	_, _, err := runFuzzer(t, "fail/notfound", nil, FuzzerNormal)
	if err == nil || !strings.Contains(err.Error(), "not found") {
		t.Fatalf("expected failure to find package but got: %s", err)
	}
}
