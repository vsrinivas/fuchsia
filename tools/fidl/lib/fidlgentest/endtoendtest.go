// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgentest

import (
	"bytes"
	"context"
	"flag"
	"fmt"
	"io/ioutil"
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

	var (
		base        = t.TempDir()
		dotFidlFile = filepath.Join(base, "main.fidl")
		dotJSONFile = filepath.Join(base, "main.fidl.json")
		params      = []string{
			"--json", dotJSONFile,
			"--experimental", "new_syntax_only",
		}
	)

	if err := ioutil.WriteFile(dotFidlFile, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	for i, dep := range t.deps {
		depFidlFile := filepath.Join(base, fmt.Sprintf("dep_%d.fidl", i))
		if err := ioutil.WriteFile(depFidlFile, []byte(dep), 0o600); err != nil {
			t.Fatal(err)
		}
		params = append(params, "--files", depFidlFile)
	}

	params = append(params, "--files", dotFidlFile)
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

	root, err := fidlgen.ReadJSONIr(dotJSONFile)
	if err != nil {
		t.Fatal(err)
	}

	return root
}
