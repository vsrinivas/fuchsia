// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package avb

import (
	"bytes"
	"context"
	"fmt"
	"io/ioutil"
	"path/filepath"
	"strings"
	"testing"
)

// createScript returns the path to a bash script that outputs its name and
// all its arguments.
func createScript(t *testing.T) string {
	name := filepath.Join(t.TempDir(), "avbtool.sh")
	contents := "#!/bin/bash\necho \"$0 $@\"\n"
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

func checkContains(t *testing.T, name string, needle string, haystack []string) {
	for _, s := range haystack {
		if s == needle {
			return
		}
	}
	t.Fatalf("%s check failed. List did not contain \"%s\".", name, needle)
}

func TestNoProperties(t *testing.T) {
	avbtoolScript := createScript(t)
	dir := t.TempDir()
	avbKey := filepath.Join(dir, "avbkey")
	if err := ioutil.WriteFile(avbKey, nil, 0o600); err != nil {
		t.Fatal(err)
	}
	avbMetadata := filepath.Join(dir, "avbmetadata")
	if err := ioutil.WriteFile(avbMetadata, nil, 0o600); err != nil {
		t.Fatal(err)
	}
	var output bytes.Buffer
	avbTool, err := newAVBToolWithStdout(avbtoolScript, avbKey, avbMetadata, &output)
	if err != nil {
		t.Fatalf("Failed to create AVBTool: %s", err)
	}

	err = avbTool.MakeVBMetaImage(context.Background(), filepath.Join("destination", "path"), filepath.Join("source", "path"), map[string]string{})
	if err != nil {
		t.Fatalf("Failed to add properties: %s", err)
	}

	args := strings.Split(strings.TrimSpace(output.String()), " ")
	if len(args) < 2 {
		t.Fatal("Too few arguments.")
	}
	checkEq(t, "command", args[1], "make_vbmeta_image")

	var destPath string
	var srcPath string
	var keyPath string
	var keyMetadataPath string
	var algorithm string
	for i, arg := range args {
		if arg == "--output" {
			if i+1 < len(args) {
				destPath = args[i+1]
			}
		} else if arg == "--key" {
			if i+1 < len(args) {
				keyPath = args[i+1]
			}
		} else if arg == "--algorithm" {
			if i+1 < len(args) {
				algorithm = args[i+1]
			}
		} else if arg == "--public_key_metadata" {
			if i+1 < len(args) {
				keyMetadataPath = args[i+1]
			}
		} else if arg == "--include_descriptors_from_image" {
			if i+1 < len(args) {
				srcPath = args[i+1]
			}
		}
	}

	checkEq(t, "source path", srcPath, "source/path")
	checkEq(t, "destination path", destPath, "destination/path")
	checkEq(t, "key path", keyPath, avbKey)
	checkEq(t, "key metadata path", keyMetadataPath, avbMetadata)
	checkEq(t, "algorithm", algorithm, "SHA512_RSA4096")
}

func TestProperties(t *testing.T) {
	avbtoolScript := createScript(t)
	dir := t.TempDir()
	avbKey := filepath.Join(dir, "avbkey")
	if err := ioutil.WriteFile(avbKey, nil, 0o600); err != nil {
		t.Fatal(err)
	}
	avbMetadata := filepath.Join(dir, "avbmetadata")
	if err := ioutil.WriteFile(avbMetadata, nil, 0o600); err != nil {
		t.Fatal(err)
	}
	var output bytes.Buffer
	avbTool, err := newAVBToolWithStdout(avbtoolScript, avbKey, avbMetadata, &output)
	if err != nil {
		t.Fatalf("Failed to create AVBTool: %s", err)
	}

	propFile1 := filepath.Join(dir, "prop1")
	propFile2 := filepath.Join(dir, "prop2")
	propFile3 := filepath.Join(dir, "prop3")
	for _, p := range []string{propFile1, propFile2, propFile3} {
		if err := ioutil.WriteFile(p, nil, 0o600); err != nil {
			t.Fatal(err)
		}
	}
	err = avbTool.MakeVBMetaImage(context.Background(), "destination/path", "source/path", map[string]string{
		"key1": propFile1,
		"key2": propFile2,
		"key3": propFile3,
	})
	if err != nil {
		t.Fatalf("Failed to add properties: %s", err)
	}

	args := strings.Split(strings.TrimSpace(output.String()), " ")
	if len(args) < 2 {
		t.Fatal("Too few arguments.")
	}
	checkEq(t, "command", args[1], "make_vbmeta_image")

	var destPath string
	var srcPath string
	var keyPath string
	var keyMetadataPath string
	var algorithm string
	var prop_from_files []string
	for i, arg := range args {
		if arg == "--output" {
			if i+1 < len(args) {
				destPath = args[i+1]
			}
		} else if arg == "--key" {
			if i+1 < len(args) {
				keyPath = args[i+1]
			}
		} else if arg == "--algorithm" {
			if i+1 < len(args) {
				algorithm = args[i+1]
			}
		} else if arg == "--public_key_metadata" {
			if i+1 < len(args) {
				keyMetadataPath = args[i+1]
			}
		} else if arg == "--include_descriptors_from_image" {
			if i+1 < len(args) {
				srcPath = args[i+1]
			}
		} else if arg == "--prop_from_file" {
			if i+1 < len(args) {
				prop_from_files = append(prop_from_files, args[i+1])
			}
		}
	}

	checkEq(t, "source path", srcPath, "source/path")
	checkEq(t, "destination path", destPath, "destination/path")
	checkEq(t, "key path", keyPath, avbKey)
	checkEq(t, "key metadata path", keyMetadataPath, avbMetadata)
	checkEq(t, "algorithm", algorithm, "SHA512_RSA4096")

	if len(prop_from_files) != 3 {
		t.Fatal("Incorrect number of prop_from_file arguments")
	}
	checkContains(t, "first property", fmt.Sprintf("key1:%s", propFile1), prop_from_files)
	checkContains(t, "second property", fmt.Sprintf("key2:%s", propFile2), prop_from_files)
	checkContains(t, "third property", fmt.Sprintf("key3:%s", propFile3), prop_from_files)
}
