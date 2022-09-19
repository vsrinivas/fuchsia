// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package goldentest

import (
	"context"
	"flag"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
)

var (
	testgen  = flag.String("testgen", "", "Path to autotest binary")
	cm       = flag.String("cm", "", "Path to cm file")
	golden   = flag.String("golden", "", "Path to golden file directory for rust")
	language = flag.String("language", "", "Accepts 'cpp' or 'rust'")
	mock     = flag.Bool("mock", false, "Whether to generate mocks")

	output string
)

func setUp(t *testing.T) {
	if _, err := os.Stat(*testgen); os.IsNotExist(err) {
		t.Fatalf("Invalid testgen path %q err: %s", *testgen, err)
	}
	if _, err := os.Stat(*cm); os.IsNotExist(err) {
		t.Fatalf("Invalid cm path %q err: %s", *cm, err)
	}
	if _, err := os.Stat(*golden); os.IsNotExist(err) {
		t.Fatalf("Invalid golden path %q err: %s", *golden, err)
	}
	if *language != "rust" && *language != "cpp" {
		t.Fatalf("Incorrect language %q allowed values are 'cpp' or 'rust'", *language)
	}
}

func runTestGen(t *testing.T) {
	testgen_abs, err := filepath.Abs(*testgen)
	if err != nil {
		t.Fatalf("Cannot find absolute path to testgen binary %s, error: %v", *testgen, err)
	}
	cm_abs, err := filepath.Abs(*cm)
	if err != nil {
		t.Fatalf("Cannot find absolute path to cm %s, error: %v", *cm, err)
	}

	td := t.TempDir()
	output = filepath.Join(td, t.Name())
	t.Logf("output %s", output)
	args := []string{
		"--cm-location", cm_abs, "--out-dir", output,
	}
	if *language == "cpp" {
		args = append(args, "--cpp")
	}
	if *mock {
		args = append(args, "--generate-mocks")
	}
	cmd := exec.CommandContext(
		context.Background(),
		testgen_abs,
		args...,
	)
	cmd.Dir = td
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		t.Fatal(err)
	}
}

func diff(t *testing.T, generated_path, golden_path string) {
	generated_code, err := os.ReadFile(generated_path)
	if err != nil {
		t.Fatal(err)
	}
	golden_code, err := os.ReadFile(golden_path)
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(strings.Join(strings.Split(string(golden_code), "\n")[1:], "\n"), strings.Join(strings.Split(string(generated_code), "\n")[1:], "\n")); diff != "" {
		t.Errorf("Found mismatch in generated code (-want +got):\n%s", diff)
	}
}

func compareGolden(t *testing.T) {
	t.Logf("generated_build %s", output)
	t.Logf("golden_build %s", *golden)
	golden_abs, err := filepath.Abs(*golden)
	if err != nil {
		t.Fatalf("Cannot find absolute path to golden %s, error: %v", *golden, err)
	}
	// Build
	diff(t, filepath.Join(output, "BUILD.gn"), filepath.Join(golden_abs, "BUILD.gn.golden"))
	if strings.Contains(*cm, "echo_server.cm") {
		// Manifest
		diff(t, filepath.Join(output, "meta/echo_server_test.cml"), filepath.Join(golden_abs, "echo_server_test.cml.golden"))
	} else {
		// Manifest
		diff(t, filepath.Join(output, "meta/echo_client_test.cml"), filepath.Join(golden_abs, "echo_client_test.cml.golden"))
	}
	// Code
	if *language == "rust" {
		if *mock {
			diff(t, filepath.Join(output, "src/echo_client_test.rs"), filepath.Join(golden_abs, "echo_client_test.rs.golden"))
		} else {
			diff(t, filepath.Join(output, "src/echo_server_test.rs"), filepath.Join(golden_abs, "echo_server_test.rs.golden"))
		}
		diff(t, filepath.Join(output, "src/testgen.rs"), filepath.Join(golden_abs, "testgen.rs.golden"))
	}
	if *language == "cpp" {
		diff(t, filepath.Join(output, "src/echo_server_test.cc"), filepath.Join(golden_abs, "echo_server_test.cc.golden"))
	}
}

func Test_Golden(t *testing.T) {
	setUp(t)
	runTestGen(t)
	compareGolden(t)
}
