// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/json"
	"flag"
	"io"
	"io/ioutil"
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
	instrumentedBinary = flag.String("instrumented-binary", "", "Path to the instrumented coverage test binary")
	goldenCoverageFile = flag.String("golden-coverage-file", "", "Path to golden coverage file")
	llvmCov            = flag.String("llvm-cov", "", "Path to llvm-cov tool")
	llvmProfData       = flag.String("llvm-profdata", "", "Path to llvm-profdata tool")
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
	// This is v2 test, which uses run-test-suite instead of runtests, so runtests=false.
	// TODO(fxbug.dev/77634): When we start treating profiles as artifacts, start using ffx
	// with testrunner.NewFFXTester().
	tester, err := testrunner.NewFuchsiaSSHTester(ctx, addr, sshKeyFile, testOutDir, "", false)
	if err != nil {
		t.Fatalf("failed to initialize fuchsia tester: %s", err)
	}

	test := testsharder.Test{
		Test: build.Test{
			Name:       "fuchsia-pkg://fuchsia.com/coverage_test_package#meta/coverage_test_package.cm",
			PackageURL: "fuchsia-pkg://fuchsia.com/coverage_test_package#meta/coverage_test_package.cm",
		},
		RunAlgorithm: testsharder.StopOnFailure,
		Runs:         1,
	}

	// Run the test over SSH.
	_, err = tester.Test(ctx, test, os.Stdout, os.Stdout, "unused-out-dir")
	if err != nil {
		t.Fatalf("failed to run the test: %s", err)
	}

	var sinks []runtests.DataSinkReference
	// Create a test outputs object, responsible for producing TAP output,
	// and recording data sinks.
	outputs, err := testrunner.CreateTestOutputs(tap.NewProducer(io.Discard), testOutDir)
	if err != nil {
		t.Fatalf("failed to create test outputs: %s", err)
	}

	profileDir := filepath.Join(testOutDir, "v2/llvm-profile")
	var files []os.FileInfo

	// Copy profiles to the host. There might be a delay between when the test finishes and
	// data sinks including profiles are available on the target to copy. When that's the case, retry.
	// TODO(fxbug.dev/77634): When we start treating profiles as artifacts, remove retry.
	err = retry.Retry(ctx, retry.NewConstantBackoff(5*time.Second), func() error {
		err = tester.EnsureSinks(ctx, sinks, outputs)
		if err != nil {
			return err
		}
		// Read the directory that contains the copied profiles.
		// If the directory does not exist yet, retry.
		files, err = ioutil.ReadDir(profileDir)
		if err != nil {
			return err
		}
		return nil
	}, nil)
	if err != nil {
		t.Fatalf("failed to collect data sinks: %s", err)
	}

	// Find the raw profile.
	rawProfile := ""
	for _, file := range files {
		filePath := filepath.Join(profileDir, file.Name())
		if filepath.Ext(filePath) == ".profraw" {
			rawProfile = filePath
			break
		}
	}
	if rawProfile == "" {
		t.Fatalf("failed to find a raw profile")
	}

	// Read the raw profile using llvm-profdata show command to ensure that it's valid.
	args := []string{
		"show",
		"--binary-ids",
		rawProfile,
	}
	showCmd := exec.Command(*llvmProfData, args...)
	err = showCmd.Run()
	if err != nil {
		t.Fatalf("profile is malformed: %s", err)
	}

	// Generate an indexed profile using llvm-profdata merge command.
	indexedProfile := filepath.Join(profileDir, "coverage.profdata")
	args = []string{
		"merge",
		rawProfile,
		"-o",
		indexedProfile,
	}
	mergeCmd := exec.Command(*llvmProfData, args...)
	err = mergeCmd.Run()
	if err != nil {
		t.Fatalf("cannot create an indexed profile: %s", err)
	}

	// Generate a coverage report via using llvm-cov export command.
	args = []string{
		"export",
		"-summary-only",
		"-format=text",
		"-instr-profile", indexedProfile,
		*instrumentedBinary,
	}
	exportCmd := exec.Command(*llvmCov, args...)
	generatedCoverage, err := exportCmd.CombinedOutput()
	if err != nil {
		t.Fatalf("cannot export coverage: %s", err)
	}
	var generatedCoverageExport llvm.Export
	err = json.Unmarshal(generatedCoverage, &generatedCoverageExport)
	if err != nil {
		t.Fatalf("cannot unmarshall generated coverage: %s", err)
	}

	// Read golden coverage report.
	goldenCoverage, err := os.ReadFile(*goldenCoverageFile)
	if err != nil {
		t.Fatalf("cannot find golden coverage report: %s", err)
	}
	var goldenCoverageExport llvm.Export
	err = json.Unmarshal(goldenCoverage, &goldenCoverageExport)
	if err != nil {
		t.Fatalf("cannot unmarshall golden coverage: %s", err)
	}

	// Compare the generated coverage report with a golden coverage report.
	diff := cmp.Diff(generatedCoverageExport, goldenCoverageExport)
	if diff != "" {
		t.Fatalf("unexpected coverage (-golden-coverage +generated-coverage): %s", diff)
	}
}
