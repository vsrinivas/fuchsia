// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"io"
	"net"
	"os"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/integration/testsharder"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/tap"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner"
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
	// This test currently does use ffx, so ffx=nil.
	// TODO(fxbug.dev/77634): When we start treating profiles as artifacts, start using ffx.
	tester, err := testrunner.NewFuchsiaSSHTester(ctx, addr, sshKeyFile, testOutDir, "", false, nil)
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
	outputs := testrunner.CreateTestOutputs(tap.NewProducer(io.Discard), testOutDir)
	// Copy profiles to the host.
	err = tester.EnsureSinks(ctx, sinks, outputs)
	if err != nil {
		t.Fatalf("failed to collect data sinks: %s", err)
	}
}
