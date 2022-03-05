// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/json"
	"flag"
	"io"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/debug/covargs/api/llvm"
	"go.fuchsia.dev/fuchsia/tools/integration/testsharder"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/tap"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner"
)

var (
	coverageTestBinary = flag.String("coverage-test-binary", "", "Path to the instrumented coverage test binary")
	coverageTestName   = flag.String("coverage-test-name", "", "Name of coverage test")
	goldenCoverageFile = flag.String("golden-coverage", "", "Path to golden coverage file")
	llvmProfData       = flag.String("llvm-profdata", "", "Path to version of llvm-profdata tool")
	llvmCov            = flag.String("llvm-cov", "", "Path to llvm-cov tool")
)

func TestCoverage(t *testing.T) {
	ctx := context.Background()
	var addr net.IPAddr
	if deviceAddr, ok := os.LookupEnv(constants.DeviceAddrEnvKey); ok {
		addrPtr, err := net.ResolveIPAddr("ip", deviceAddr)
		if err != nil {
			t.Fatalf("failed to parse device address: %s", deviceAddr)
		}
		addr = *addrPtr
	}

	// Read SSH key which is required to run a test.
	sshKeyFile := os.Getenv(constants.SSHKeyEnvKey)
	testOutDir := t.TempDir()

	// Create a new fuchsia tester that is responsible for executing the test.
	// This is a v2 test, and it uses run-test-suite instead of runtests, so runtests=false.
	// TODO(fxbug.dev/77634): When we start treating profiles as artifacts, start using ffx
	// with testrunner.NewFFXTester().
	tester, err := testrunner.NewFuchsiaSSHTester(ctx, addr, sshKeyFile, testOutDir, "", false)
	if err != nil {
		t.Fatalf("failed to initialize fuchsia tester: %s", err)
	}

	test := testsharder.Test{
		Test: build.Test{
			Name:       *coverageTestName,
			PackageURL: *coverageTestName,
		},
		RunAlgorithm: testsharder.StopOnFailure,
		Runs:         1,
	}
	// Run the test over SSH.
	result, err := tester.Test(ctx, test, os.Stdout, os.Stdout, "unused-out-dir")
	if err != nil {
		t.Fatalf("failed to run the test: %s", err)
	}

	// Create a test outputs object, responsible for producing TAP output,
	// and recording data sinks.
	outputs, err := testrunner.CreateTestOutputs(tap.NewProducer(io.Discard), testOutDir)
	if err != nil {
		t.Fatalf("failed to create test outputs: %s", err)
	}

	// Record data sinks.
	if err := outputs.Record(ctx, *result); err != nil {
		t.Fatalf("failed to record data sinks: %s", err)
	}

	// Copy profiles to the host. There might be a delay between when the test finishes and
	// data sinks including profiles are available on the target to copy. When that's the case,
	// EnsureSinks() does not return an error, and it logs the message.
	// Therefore, check whether v2 sinks directory exists to ensure copying is successful.
	// When there is a delay, retry.
	// TODO(fxbug.dev/77634): When we start treating profiles as artifacts, remove retry.
	var sinks []runtests.DataSinkReference
	err = retry.Retry(ctx, retry.NewConstantBackoff(5*time.Second), func() error {
		if err := tester.EnsureSinks(ctx, sinks, outputs); err != nil {
			return err
		}
		v2SinksDir := filepath.Join(testOutDir, "v2")
		_, err := os.ReadDir(v2SinksDir)
		if err != nil {
			return err
		}
		return nil
	}, nil)
	if err != nil {
		t.Fatalf("failed to collect data sinks: %s", err)
	}

	// Find the raw profile that corresponds to the given coverage test name.
	rawProfile := ""
	if len(outputs.Summary.Tests) != 1 {
		t.Fatalf("failed to find the test in the outputs")
	}
	outputTest := outputs.Summary.Tests[0]
	for _, sinks := range outputTest.DataSinks {
		// There should be one sink per test.
		if len(sinks) != 1 {
			t.Fatalf("there should be one sink per test")
		}
		rawProfile = filepath.Join(testOutDir, sinks[0].File)
	}
	if rawProfile == "" {
		t.Fatalf("failed to find a raw profile")
	}

	// Read the raw profile using llvm-profdata show command to ensure that it's valid.
	args := []string{
		"show",
		"-binary-ids",
		rawProfile,
	}
	showCmd := exec.Command(*llvmProfData, args...)
	if showCmdOutput, err := showCmd.CombinedOutput(); err != nil {
		if _, ok := err.(*exec.ExitError); ok {
			t.Fatalf("cannot read raw profile %s: %s", rawProfile, string(showCmdOutput))
		} else {
			t.Fatalf("cannot execute %s: %s", showCmd, err)
		}
	}

	// Generate an indexed profile using llvm-profdata merge command.
	indexedProfile := filepath.Join(testOutDir, "coverage.profdata")
	args = []string{
		"merge",
		rawProfile,
		"-o",
		indexedProfile,
	}
	mergeCmd := exec.Command(*llvmProfData, args...)
	if mergeCmdOutput, err := mergeCmd.CombinedOutput(); err != nil {
		if _, ok := err.(*exec.ExitError); ok {
			t.Fatalf("cannot create an indexed profile: %s", string(mergeCmdOutput))
		} else {
			t.Fatalf("cannot execute %s: %s", mergeCmd, err)
		}
	}

	// Generate a coverage report via using llvm-cov export command.
	args = []string{
		"export",
		"-summary-only",
		"-format=text",
		"-instr-profile", indexedProfile,
		*coverageTestBinary,
	}
	exportCmd := exec.Command(*llvmCov, args...)
	generatedCoverageOutput, err := exportCmd.CombinedOutput()
	if err != nil {
		if _, ok := err.(*exec.ExitError); ok {
			t.Fatalf("cannot export coverage: %s", string(generatedCoverageOutput))
		} else {
			t.Fatalf("cannot execute %s: %s", exportCmd, err)
		}
	}
	var generatedCoverageExport llvm.Export
	if err := json.Unmarshal(generatedCoverageOutput, &generatedCoverageExport); err != nil {
		t.Fatalf("cannot unmarshal generated coverage: %s", err)
	}

	// Read golden coverage report.
	goldenCoverage, err := os.ReadFile(*goldenCoverageFile)
	if err != nil {
		t.Fatalf("cannot find golden coverage report: %s", err)
	}
	var goldenCoverageExport llvm.Export
	if err := json.Unmarshal(goldenCoverage, &goldenCoverageExport); err != nil {
		t.Fatalf("cannot unmarshal golden coverage: %s", err)
	}

	// Compare the generated coverage report with a golden coverage report.
	diff := cmp.Diff(goldenCoverageExport, generatedCoverageExport)
	if diff != "" {
		t.Fatalf("unexpected coverage (-golden-coverage +generated-coverage): %s", diff)
	}

	// Close recording of test outputs.
	if err := outputs.Close(); err != nil {
		t.Fatalf("failed to save test outputs: %s", err)
	}
}
