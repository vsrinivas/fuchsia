// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/hex"
	"encoding/json"
	"flag"
	"io"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/debug/covargs/api/llvm"
	"go.fuchsia.dev/fuchsia/tools/integration/testsharder"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/tap"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner"
)

var (
	covargs            = flag.String("covargs", "", "Path to covargs binary")
	coverageTestBinary = flag.String("coverage-test-binary", "", "Path to instrumented coverage test binary")
	coverageTestName   = flag.String("coverage-test-name", "", "Name of coverage test")
	goldenCoverageFile = flag.String("golden-coverage", "", "Path to golden coverage file")
	host               = flag.Bool("host", false, "If set, run coverage test on host")
	llvmCov            = flag.String("llvm-cov", "", "Path to llvm-cov tool")
	llvmProfData       = flag.String("llvm-profdata", "", "Path to version of llvm-profdata tool")
)

func TestCoverage(t *testing.T) {
	testOutDir := t.TempDir()
	rawProfile := runCoverageTest(t, testOutDir)
	// Prepare llvm-profdata arguments.
	args := []string{
		"show",
		"-binary-ids",
		rawProfile,
	}
	// Read the raw profile using llvm-profdata show command to ensure that it's valid.
	showCmd := exec.Command(*llvmProfData, args...)
	showCmdOutput, err := showCmd.CombinedOutput()
	if err != nil {
		if _, ok := err.(*exec.ExitError); ok {
			t.Fatalf("cannot read raw profile %s: %s", rawProfile, string(showCmdOutput))
		} else {
			t.Fatalf("cannot execute %q: %s", showCmd, err)
		}
	}

	// Prepare llvm-profdata arguments.
	indexedProfile := filepath.Join(testOutDir, "coverage.profdata")
	args = []string{
		"merge",
		rawProfile,
		"-o",
		indexedProfile,
	}
	// Generate an indexed profile using llvm-profdata merge command.
	mergeCmd := exec.Command(*llvmProfData, args...)
	if mergeCmdOutput, err := mergeCmd.CombinedOutput(); err != nil {
		if _, ok := err.(*exec.ExitError); ok {
			t.Fatalf("cannot create an indexed profile: %s", string(mergeCmdOutput))
		} else {
			t.Fatalf("cannot execute %q: %s", mergeCmd, err)
		}
	}

	// Prepare llvm-cov arguments.
	args = []string{
		"export",
		"-summary-only",
		"-format=text",
		"-instr-profile", indexedProfile,
		*coverageTestBinary,
	}
	// Generate a coverage report via using llvm-cov export command.
	exportCmd := exec.Command(*llvmCov, args...)
	generatedCoverageOutput, err := exportCmd.CombinedOutput()
	if err != nil {
		if _, ok := err.(*exec.ExitError); ok {
			t.Fatalf("cannot export coverage: %s", string(generatedCoverageOutput))
		} else {
			t.Fatalf("cannot execute %q: %s", exportCmd, err)
		}
	}
	var generatedCoverageExport llvm.Export
	if err := json.Unmarshal(generatedCoverageOutput, &generatedCoverageExport); err != nil {
		t.Fatalf("cannot unmarshal generated coverage: %s", err)
	}

	// Read golden coverage.
	goldenCoverage, err := os.ReadFile(*goldenCoverageFile)
	if err != nil {
		t.Fatalf("cannot find golden coverage file: %s", err)
	}
	var goldenCoverageExport llvm.Export
	if err := json.Unmarshal(goldenCoverage, &goldenCoverageExport); err != nil {
		t.Fatalf("cannot unmarshal golden coverage: %s", err)
	}

	// Compare the generated coverage with a golden coverage.
	diff := cmp.Diff(goldenCoverageExport, generatedCoverageExport)
	if diff != "" {
		t.Fatalf("unexpected coverage (-golden-coverage +generated-coverage): %s", diff)
	}

	summaryFile := filepath.Join(testOutDir, "summary.json")
	if _, err := os.Stat(summaryFile); err != nil {
		t.Fatalf("failed to find summary.json: %s", err)
	}

	splittedOutput := strings.Split((string(showCmdOutput)), "\n")
	if len(splittedOutput) < 2 {
		t.Fatalf("invalid build id in profile %q: %s", rawProfile, err)
	}
	embeddedBuildId := splittedOutput[len(splittedOutput)-2]
	// Check if embedded build id consists of hex characters.
	if _, err = hex.DecodeString(embeddedBuildId); err != nil {
		t.Fatalf("invalid build id in profile %q: %s", rawProfile, err)
	}

	// Create a debug file in the format of xx/yyyyyyyy.debug for covargs.
	debugFile := filepath.Join(testOutDir, embeddedBuildId[:2], embeddedBuildId[2:]+".debug")
	if err := osmisc.CopyFile(*coverageTestBinary, debugFile); err != nil {
		t.Fatalf("failed to create a debug file: %s", err)
	}

	// Prepare covargs arguments.
	args = []string{
		"-build-id-dir", testOutDir,
		"-llvm-cov", *llvmCov,
		"-llvm-profdata", *llvmProfData,
		"-output-dir", testOutDir,
		"-coverage-report=false",
		"-report-dir", testOutDir,
		"-save-temps", testOutDir,
		"-summary", summaryFile,
	}

	// Invoke covargs.
	covargsCmd := exec.Command(*covargs, args...)
	if covargsOutput, err := covargsCmd.CombinedOutput(); err != nil {
		if _, ok := err.(*exec.ExitError); ok {
			t.Fatalf("failed to run covargs: %s", string(covargsOutput))
		} else {
			t.Fatalf("cannot execute %q: %s", covargsCmd, err)
		}
	}

	// Read generated coverage.
	coverageFile := filepath.Join(testOutDir, "coverage.json")
	generatedCoverage, err := os.ReadFile(coverageFile)
	if err != nil {
		t.Fatalf("cannot read coverage.json: %s", err)
	}
	if err := json.Unmarshal(generatedCoverage, &generatedCoverageExport); err != nil {
		t.Fatalf("cannot unmarshal generated coverage: %s", err)
	}

	if len(goldenCoverageExport.Data) != 1 || len(generatedCoverageExport.Data) != 1 {
		t.Fatalf("failed to export data")
	}

	// Compare the covargs generated coverage summary section with a golden coverage.
	diff = cmp.Diff(goldenCoverageExport.Data[0].Totals, generatedCoverageExport.Data[0].Totals)
	if diff != "" {
		t.Fatalf("unexpected coverage (-golden-coverage +generated-coverage): %s", diff)
	}
}

func runCoverageTest(t *testing.T, testOutDir string) string {
	var test testsharder.Test
	var tester testrunner.Tester
	if *host {
		test = getHostTest()
		tester = getHostTester(t, testOutDir)
	} else {
		test = getTargetTest()
		tester = getTargetTester(t, testOutDir)
	}

	defer tester.Close()
	ctx := context.Background()
	result, err := tester.Test(ctx, test, os.Stdout, os.Stdout, "unused-out-dir")
	if err != nil {
		t.Fatalf("failed to run the test: %s", err)
	}
	outputs, err := testrunner.CreateTestOutputs(tap.NewProducer(io.Discard), testOutDir)
	if err != nil {
		t.Fatalf("failed to create test outputs: %s", err)
	}
	// Record data sinks.
	if err := outputs.Record(ctx, *result); err != nil {
		t.Fatalf("failed to record data sinks: %s", err)
	}

	// If running on target, copy profiles to the host. There might be a delay between when
	// the test finishes and data sinks including profiles are available on the target to copy.
	// When that's the case, EnsureSinks() does not return an error, and it logs the message.
	// Therefore, check whether v2 sinks directory exists to ensure copying is successful.
	// When there is a delay, retry.
	// TODO(fxbug.dev/77634): When we start treating profiles as artifacts, remove retry.
	if !*host {
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
	}

	// Close recording of test outputs.
	if err := outputs.Close(); err != nil {
		t.Fatalf("failed to save test outputs: %s", err)
	}

	var rawProfile string
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

	return rawProfile
}

func getHostTest() testsharder.Test {
	test := testsharder.Test{
		Test: build.Test{
			Path: *coverageTestBinary,
		},
		RunAlgorithm: testsharder.StopOnFailure,
		Runs:         1,
	}

	return test
}

func getHostTester(t *testing.T, testOutDir string) testrunner.Tester {
	wd, err := os.Getwd()
	if err != nil {
		t.Fatalf("failed to get current working directory: %s", err)
	}
	tester, err := testrunner.NewSubprocessTester(wd, os.Environ(), testOutDir, "", "")
	if err != nil {
		t.Fatalf("failed to initialize fuchsia tester: %s", err)
	}

	return tester
}

func getTargetTest() testsharder.Test {
	test := testsharder.Test{
		Test: build.Test{
			Name:       *coverageTestName,
			PackageURL: *coverageTestName,
		},
		RunAlgorithm: testsharder.StopOnFailure,
		Runs:         1,
	}
	return test
}

func getTargetTester(t *testing.T, testOutDir string) testrunner.Tester {
	var addr net.IPAddr
	if deviceAddr, ok := os.LookupEnv(constants.DeviceAddrEnvKey); ok {
		addrPtr, err := net.ResolveIPAddr("ip", deviceAddr)
		if err != nil {
			t.Fatalf("failed to parse device address: %s", deviceAddr)
		}
		addr = *addrPtr
	}

	ctx := context.Background()
	// Read SSH key which is required to run a test.
	sshKeyFile := os.Getenv(constants.SSHKeyEnvKey)
	// Create a new fuchsia tester that is responsible for executing the test.
	// This is a v2 test, and it uses run-test-suite instead of runtests, so runtests=false.
	// TODO(fxbug.dev/77634): When we start treating profiles as artifacts, start using ffx
	// with testrunner.NewFFXTester().
	tester, err := testrunner.NewFuchsiaSSHTester(ctx, addr, sshKeyFile, testOutDir, "", false)
	if err != nil {
		t.Fatalf("failed to initialize fuchsia tester: %s", err)
	}

	return tester
}
