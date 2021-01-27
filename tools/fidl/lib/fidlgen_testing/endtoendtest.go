// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_testing

import (
	"bytes"
	"context"
	"flag"
	"io/ioutil"
	"os"
	"os/exec"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// EndToEndTest simplifies the creation of end-to-end tests which compile a FIDL
// library, and produce JSON IR, read back in Go using fidlgen.
//
// Example usage:
//
//     root := EndToEndTest{T: t}.Single(`library example; struct MyStruct {};`)
//
// If dependencies are needed:
//
//     root := EndToEndTest{T: t}
//         .WithDependency(`library dep; struct S{};`)
//         .Single(`library example; struct MyStruct{ dep.S foo};`)
type EndToEndTest struct {
	*testing.T
	deps []string
}

var fidlcPath = flag.String("fidlc", "", "Path to fidlc.")

// WithDependency adds the source text for a dependency.
func (t EndToEndTest) WithDependency(content string) EndToEndTest {
	t.deps = append(t.deps, content)
	return t
}

// Single compiles a single FIDL file, and returns a Root.
func (t EndToEndTest) Single(content string) fidlgen.Root {
	ctx, cancel := context.WithTimeout(context.Background(), 1*time.Minute)
	defer cancel()

	dotFidlFile, err := ioutil.TempFile("", "*.fidl")
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(dotFidlFile.Name())
	if _, err := dotFidlFile.Write([]byte(content)); err != nil {
		t.Fatal(err)
	}
	if err := dotFidlFile.Close(); err != nil {
		t.Fatal(err)
	}

	dotJSONFile, err := ioutil.TempFile("", "*.fidl.json")
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(dotJSONFile.Name())
	if err := dotJSONFile.Close(); err != nil {
		t.Fatal(err)
	}

	params := []string{
		"--json", dotJSONFile.Name(),
	}

	for _, dep := range t.deps {
		depFidlFile, err := ioutil.TempFile("", "*.fidl")
		if err != nil {
			t.Fatal(err)
		}
		defer os.Remove(depFidlFile.Name())
		defer func() {
			if err := depFidlFile.Close(); err != nil {
				t.Fatal(err)
			}
		}()
		if _, err := depFidlFile.Write([]byte(dep)); err != nil {
			t.Fatal(err)
		}
		params = append(params, "--files", depFidlFile.Name())
	}

	params = append(params, "--files", dotFidlFile.Name())
	var (
		cmd         = exec.CommandContext(ctx, *fidlcPath, params...)
		fidlcStdout = new(bytes.Buffer)
		fidlcStderr = new(bytes.Buffer)
	)
	cmd.Stdout = fidlcStdout
	cmd.Stderr = fidlcStderr

	if err := cmd.Run(); err != nil {
		t.Logf("fidlc cmdline: %v %v", *fidlcPath, params)
		t.Logf("fidlc stdout: %s", fidlcStdout.String())
		t.Logf("fidlc stderr: %s", fidlcStderr.String())
		t.Fatal(err)
	}

	root, err := fidlgen.ReadJSONIr(dotJSONFile.Name())
	if err != nil {
		t.Fatal(err)
	}

	return root
}
