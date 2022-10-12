// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgentest

import (
	"bytes"
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// EndToEndTest simplifies the creation of end-to-end tests which compile a FIDL
// library, and produce JSON IR, read back in Go using fidlgen.
//
// Example usage:
//
//	root := EndToEndTest{T: t}.Single(`library example; struct MyStruct {};`)
//
// If dependencies are needed:
//
//	root := EndToEndTest{T: t}
//	    .WithDependency(`library dep; struct S{};`)
//	    .Single(`library example; struct MyStruct{ dep.S foo};`)
type EndToEndTest struct {
	*testing.T
	wd         string
	deps       []string
	experiment []string
}

var fidlc = flag.String("fidlc", "", "Path to fidlc.")

// WithDependency adds the source text for a dependency.
func (t EndToEndTest) WithDependency(content string) EndToEndTest {
	t.deps = append(t.deps, content)
	return t
}

// WithExperiment adds the input as an experimental flag.
func (t EndToEndTest) WithExperiment(f string) EndToEndTest {
	t.experiment = append(t.experiment, f)
	return t
}

// WithWorkingDirectory sets the working directory of any associated fidlc
// invocation. If unset, the process's current working directory will be used.
func (t EndToEndTest) WithWorkingDirectory(wd string) EndToEndTest {
	t.wd = wd
	return t
}

// Single compiles a single FIDL file, and returns a Root.
func (t EndToEndTest) Single(content string) fidlgen.Root {
	return t.Multiple([]string{content})
}

// Multiple compiles multiple FIDL files, and returns a Root.
//
// The mapping of `contents` to the list of generated FIDL source files
// preserves ordering (by index to lexicographically on filename).
func (t EndToEndTest) Multiple(contents []string) fidlgen.Root {
	if len(contents) == 0 {
		t.Fatal("no FIDL file contents provided")
	}

	ctx, cancel := context.WithTimeout(context.Background(), 1*time.Minute)
	defer cancel()

	var (
		base        = t.TempDir()
		dotJSONFile = filepath.Join(base, "main.fidl.json")
		params      = []string{
			"--json", dotJSONFile,
		}
	)

	// And one file for each dependency.
	for i, dep := range t.deps {
		f, err := os.CreateTemp(base, fmt.Sprintf("dep_%d.fidl", i))
		if err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(f.Name(), []byte(dep), 0o600); err != nil {
			f.Close()
			t.Fatal(err)
		}
		if err := f.Close(); err != nil {
			t.Fatal(err)
		}
		params = append(params, "--files", f.Name())
	}

	for _, e := range t.experiment {
		params = append(params, "--experimental", e)
	}

	// And one file for each of the given contents.
	params = append(params, "--files")
	for i, content := range contents {
		f, err := os.CreateTemp(base, fmt.Sprintf("lib_%d.fidl", i))
		if err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(f.Name(), []byte(content), 0o600); err != nil {
			f.Close()
			t.Fatal(err)
		}
		if err := f.Close(); err != nil {
			t.Fatal(err)
		}
		params = append(params, f.Name())
	}

	fidlcPath := *fidlc
	if t.wd != "" {
		// Absolutize for access in the case of an arbitrary working directory.
		var err error
		fidlcPath, err = filepath.Abs(fidlcPath)
		if err != nil {
			log.Fatalf("could not derive absolute `fidlc` path: %s", err)
		}
	}

	var (
		cmd         = exec.CommandContext(ctx, fidlcPath, params...)
		fidlcStdout = new(bytes.Buffer)
		fidlcStderr = new(bytes.Buffer)
	)
	cmd.Stdout = fidlcStdout
	cmd.Stderr = fidlcStderr
	cmd.Dir = t.wd // Uses current working directory if empty.

	if err := cmd.Run(); err != nil {
		t.Logf("fidlc cmdline: %v %v", fidlcPath, params)
		t.Logf("fidlc stdout: %s", fidlcStdout.String())
		t.Logf("fidlc stderr: %s", fidlcStderr.String())
		if t.wd != "" {
			t.Logf("fidlc working directory: %s", t.wd)
		}
		t.Fatal(err)
	}

	root, err := fidlgen.ReadJSONIr(dotJSONFile)
	if err != nil {
		t.Fatal(err)
	}

	return root
}
