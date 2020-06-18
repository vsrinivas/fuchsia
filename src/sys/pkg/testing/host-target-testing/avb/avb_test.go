// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package avb

import (
	"bytes"
	"context"
	"fmt"
	"io/ioutil"
	"os"
	"strings"
	"testing"
)

func createScript(fileName string) (script string, err error) {
	file, err := ioutil.TempFile("", fileName)
	if err != nil {
		return "", err
	}
	defer file.Close()

	// Script outputs its name and all its arguments.
	contents := `#!/bin/bash
echo "$0 $@"
`

	if _, err := file.Write([]byte(contents)); err != nil {
		os.Remove(file.Name())
		return "", err
	}

	if err := file.Chmod(0744); err != nil {
		os.Remove(file.Name())
		return "", err
	}

	return file.Name(), nil
}

func checkEq(t *testing.T, name string, actual string, expected string) {
	if actual != expected {
		t.Fatalf("%s check failed. Actual: \"%s\". Expected: \"%s\".", name, actual, expected)
	}
}

func TestNoProperties(t *testing.T) {
	avbtoolScript, err := createScript("avbtool.*.sh")
	if err != nil {
		t.Fatalf("Failed to create script: %s", err)
	}
	defer os.Remove(avbtoolScript)

	avbKey, err := ioutil.TempFile("", "")
	if err != nil {
		t.Fatalf("Failed to create key file: %s", err)
	}
	defer os.Remove(avbKey.Name())

	avbMetadata, err := ioutil.TempFile("", "")
	if err != nil {
		t.Fatalf("Failed to create metadata file: %s", err)
	}
	defer os.Remove(avbMetadata.Name())

	var output bytes.Buffer
	avbTool, err := newAVBToolWithStdout(avbtoolScript, avbKey.Name(), avbMetadata.Name(), &output)
	if err != nil {
		t.Fatalf("Failed to create AVBTool: %s", err)
	}

	err = avbTool.MakeVBMetaImage(context.Background(), "destination/path", "source/path", map[string]string{})
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
	checkEq(t, "key path", keyPath, avbKey.Name())
	checkEq(t, "key metadata path", keyMetadataPath, avbMetadata.Name())
	checkEq(t, "algorithm", algorithm, "SHA512_RSA4096")
}

func TestProperties(t *testing.T) {
	avbtoolScript, err := createScript("avbtool.*.sh")
	if err != nil {
		t.Fatalf("Failed to create script: %s", err)
	}
	defer os.Remove(avbtoolScript)

	avbKey, err := ioutil.TempFile("", "")
	if err != nil {
		t.Fatalf("Failed to create key file: %s", err)
	}
	defer os.Remove(avbKey.Name())

	avbMetadata, err := ioutil.TempFile("", "")
	if err != nil {
		t.Fatalf("Failed to create metadata file: %s", err)
	}
	defer os.Remove(avbMetadata.Name())

	var output bytes.Buffer
	avbTool, err := newAVBToolWithStdout(avbtoolScript, avbKey.Name(), avbMetadata.Name(), &output)
	if err != nil {
		t.Fatalf("Failed to create AVBTool: %s", err)
	}

	propFile1, err := ioutil.TempFile("", "")
	defer os.Remove(propFile1.Name())
	propFile2, err := ioutil.TempFile("", "")
	defer os.Remove(propFile2.Name())
	propFile3, err := ioutil.TempFile("", "")
	defer os.Remove(propFile3.Name())

	err = avbTool.MakeVBMetaImage(context.Background(), "destination/path", "source/path", map[string]string{
		"key1": propFile1.Name(),
		"key2": propFile2.Name(),
		"key3": propFile3.Name(),
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
	checkEq(t, "key path", keyPath, avbKey.Name())
	checkEq(t, "key metadata path", keyMetadataPath, avbMetadata.Name())
	checkEq(t, "algorithm", algorithm, "SHA512_RSA4096")

	if len(prop_from_files) != 3 {
		t.Fatal("Incorrect number of prop_from_file arguments")
	}
	checkEq(t, "first property", prop_from_files[0], fmt.Sprintf("key1:%s", propFile1.Name()))
	checkEq(t, "first property", prop_from_files[1], fmt.Sprintf("key2:%s", propFile2.Name()))
	checkEq(t, "first property", prop_from_files[2], fmt.Sprintf("key3:%s", propFile3.Name()))
}
