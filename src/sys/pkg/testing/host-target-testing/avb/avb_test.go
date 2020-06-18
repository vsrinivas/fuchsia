// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package avb

import (
	"bytes"
	"context"
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

	// Script outputs its name and all its arguments on the first line. Then it
	// writes all the property files to stdout too.
	contents := `#!/bin/bash
echo "$0 $@"
while (($#)); do
	case "$1" in
		--prop_from_file)
			shift
			property_file=$(cut -d':' -f2 <<< "$1")
			printf "$property_file "
			cat "$property_file"
			echo
			shift
			;;
		*) shift ;;
	esac
done
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

	err = avbTool.MakeVBMetaImage(context.Background(), "destination/path", "source/path", map[string]string{
		"key1": "value1",
		"key2": "value2",
		"key3": "value3",
	})
	if err != nil {
		t.Fatalf("Failed to add properties: %s", err)
	}

	outputs := strings.Split(output.String(), "\n")
	args := strings.Split(outputs[0], " ")

	// Collect all of the prop_from_file arguments and record the filename that
	// the value was written to.
	keyToFile := make(map[string]string)
	for i, arg := range args {
		if arg == "--prop_from_file" {
			if i+1 < len(args) {
				propArg := strings.Split(args[i+1], ":")
				if len(propArg) != 2 {
					t.Fatalf("Badly formatted prop_from_file argument: %s", args[i+1])
				}
				keyToFile[propArg[0]] = propArg[1]
			}
		}
	}

	// The bash script we passed in will write each file in prop_from_file to a
	// line containing its filename and contents.
	fileToValue := make(map[string]string)
	for _, out := range outputs {
		fileOutput := strings.Split(out, " ")
		if len(fileOutput) != 2 {
			continue
		}
		fileToValue[fileOutput[0]] = fileOutput[1]
	}

	// Finally, check that each property has the correct value.
	checkEq(t, "first property", fileToValue[keyToFile["key1"]], "value1")
	checkEq(t, "second property", fileToValue[keyToFile["key2"]], "value2")
	checkEq(t, "third property", fileToValue[keyToFile["key3"]], "value3")
}
