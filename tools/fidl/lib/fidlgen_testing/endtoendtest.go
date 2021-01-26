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
type EndToEndTest struct {
	*testing.T
}

var fidlcPath = flag.String("fidlc", "", "Path to fidlc.")

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

	var (
		cmd = exec.CommandContext(ctx, *fidlcPath,
			"--json", dotJSONFile.Name(),
			"--files", dotFidlFile.Name())
		fidlcStdout = new(bytes.Buffer)
		fidlcStderr = new(bytes.Buffer)
	)
	cmd.Stdout = fidlcStdout
	cmd.Stderr = fidlcStderr

	if err := cmd.Run(); err != nil {
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
