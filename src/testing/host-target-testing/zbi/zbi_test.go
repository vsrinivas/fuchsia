// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package zbi

import (
	"bytes"
	"context"
	"io/ioutil"
	"path/filepath"
	"strings"
	"testing"
)

// createScript returns the path to a bash script that outputs its name and
// all its arguments on the first line.
//
// Then it cats the input file, which we assume is the last argument.
func createScript(t *testing.T) string {
	contents := `#!/bin/bash
echo "$0 $@"
file="${@: -1}"
cat "$file"
`
	name := filepath.Join(t.TempDir(), "zbi.sh")
	if err := ioutil.WriteFile(name, []byte(contents), 0o700); err != nil {
		t.Fatal(err)
	}
	return name
}

func checkEq(t *testing.T, name string, actual string, expected string) {
	if actual != expected {
		t.Fatalf("%s check failed. Actual: \"%s\". Expected: \"%s\".", name, actual, expected)
	}
}

func TestNoArguments(t *testing.T) {
	var output bytes.Buffer
	zbiTool, err := NewZBIToolWithStdout(createScript(t), &output)
	if err != nil {
		t.Fatalf("Failed to create ZBI tool: %s", err)
	}

	zbiTool.MakeImageArgsZbi(context.Background(), "destination/path", map[string]string{})
	if err != nil {
		t.Fatalf("Failed to create ZBI: %s", err)
	}

	outputs := strings.Split(output.String(), "\n")
	args := strings.Split(outputs[0], " ")
	if len(args) < 1 {
		t.Fatal("Too few arguments.")
	}

	var destPath string
	var itemType string
	for i, arg := range args {
		if arg == "--output" {
			if i+1 < len(args) {
				destPath = args[i+1]
			}
		}
		if arg == "--type" {
			if i+1 < len(args) {
				itemType = args[i+1]
			}
		}
	}
	checkEq(t, "destination path", destPath, "destination/path")
	checkEq(t, "type", itemType, "IMAGE_ARGS")
}

func TestArguments(t *testing.T) {
	var output bytes.Buffer
	zbiTool, err := NewZBIToolWithStdout(createScript(t), &output)
	if err != nil {
		t.Fatalf("Failed to create ZBI tool: %s", err)
	}

	zbiTool.MakeImageArgsZbi(context.Background(), "destination/path", map[string]string{
		"key1": "value1",
		"key2": "value2",
		"key3": "value3",
	})
	if err != nil {
		t.Fatalf("Failed to create ZBI: %s", err)
	}

	outputs := strings.Split(output.String(), "\n")
	args := strings.Split(outputs[0], " ")
	if len(args) < 1 {
		t.Fatal("Too few arguments.")
	}

	var destPath string
	var itemType string
	for i, arg := range args {
		if arg == "--output" {
			if i+1 < len(args) {
				destPath = args[i+1]
			}
		}
		if arg == "--type" {
			if i+1 < len(args) {
				itemType = args[i+1]
			}
		}
	}
	checkEq(t, "destination path", destPath, "destination/path")
	checkEq(t, "type", itemType, "IMAGE_ARGS")

	imageArgs := make(map[string]string)
	for _, out := range outputs {
		keyValue := strings.Split(out, "=")
		if len(keyValue) != 2 {
			continue
		}
		imageArgs[keyValue[0]] = keyValue[1]
	}

	checkEq(t, "first property", imageArgs["key1"], "value1")
	checkEq(t, "second property", imageArgs["key2"], "value2")
	checkEq(t, "third property", imageArgs["key3"], "value3")
}
