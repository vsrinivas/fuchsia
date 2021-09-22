// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package goldentest

import (
	"context"
	"flag"
	"github.com/google/go-cmp/cmp"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
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

func compareLineByLine(t *testing.T, golden, generated []string, filename string) {
	if len(golden) != len(generated) {
		t.Errorf("Found mismatched line count for %s: golden file contains %d lines and generateed file contains %d lines", filename, len(golden), len(generated))
	}
	for i, ref := range golden {
		if diff := cmp.Diff(ref, generated[i]); diff != "" {
			t.Errorf("Found mismatch in %s (-want +got):\n%s", filename, diff)
		}
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
	generated_build, err := os.ReadFile(filepath.Join(output, "BUILD.gn"))
	if err != nil {
		t.Fatal(err)
	}
	golden_build, err := os.ReadFile(filepath.Join(golden_abs, "BUILD.gn.golden"))
	if err != nil {
		t.Fatal(err)
	}
	// Skip the first line that contains the copyright year.
	compareLineByLine(t, strings.Split(string(golden_build), "\n")[1:], strings.Split(string(generated_build), "\n")[1:], "BUILD.gn")

	// Manifest
	generated_manifest, err := os.ReadFile(filepath.Join(output, "meta/echo_server_test.cml"))
	if err != nil {
		t.Fatal(err)
	}
	golden_manifest, err := os.ReadFile(filepath.Join(golden_abs, "echo_server_test.cml.golden"))
	if err != nil {
		t.Fatal(err)
	}
	// Skip the first line that contains the copyright year.
	compareLineByLine(t, strings.Split(string(golden_manifest), "\n")[1:], strings.Split(string(generated_manifest), "\n")[1:], "manifest")

	// Code
	if *language == "rust" {
		generated_code, err := os.ReadFile(filepath.Join(output, "src/echo_server_test.rs"))
		if err != nil {
			t.Fatal(err)
		}
		golden_code, err := os.ReadFile(filepath.Join(golden_abs, "echo_server_test.rs.golden"))
		if err != nil {
			t.Fatal(err)
		}
		if diff := cmp.Diff(golden_code, generated_code); diff != "" {
			t.Errorf("Found mismatch in generated rust code (-want +got):\n%s", diff)
		}
	}
	if *language == "cpp" {
		generated_code, err := os.ReadFile(filepath.Join(output, "src/echo_server_test.cc"))
		if err != nil {
			t.Fatal(err)
		}
		golden_code, err := os.ReadFile(filepath.Join(golden_abs, "echo_server_test.cc.golden"))
		if err != nil {
			t.Fatal(err)
		}
		if diff := cmp.Diff(golden_code, generated_code); diff != "" {
			t.Errorf("Found mismatch in generated cpp code (-want +got):\n%s", diff)
		}
	}
}
func Test_Golden(t *testing.T) {
	setUp(t)
	runTestGen(t)
	compareGolden(t)
}
