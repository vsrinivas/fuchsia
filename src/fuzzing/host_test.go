// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzzing

import (
	"encoding/json"
	"io/ioutil"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"testing"
)

// Created by `fuzz_package` in //build/fuzzing/fuzzer.gni
type FuzzSpec struct {
	Host    bool     `json:"fuzz_host"`
	Package string   `json:"fuzz_package"`
	Targets []string `json:"fuzz_targets"`
}

func TestHostFuzzers(t *testing.T) {
	// Search the build directory structure for host variant fuzzer builds.
	// This is necessarily sensitive to the layout of the build directory and
	// will need to be updated if the host tests are moved or the host fuzzer
	// builds' names change.
	testBin, err := os.Executable()
	if err != nil {
		t.Fatalf("Failed to determine executable path: %v", err)
	}
	fxBuildDir := path.Dir(path.Dir(testBin))

	// Find the fuzzers that should have been built for host.
	jsonPath := path.Join(fxBuildDir, "fuzzers.json")
	_, err = os.Stat(jsonPath)
	if err != nil && os.IsNotExist(err) {
		t.SkipNow()
	}
	jsonFile, err := os.Open(jsonPath)
	if err != nil {
		t.Fatalf("Failed to find fuzzer.json: %v", err)
	}
	defer jsonFile.Close()

	jsonData, err := ioutil.ReadAll(jsonFile)
	if err != nil {
		t.Fatalf("Failed to read fuzzer.json: %v", err)
	}

	var specs []FuzzSpec
	err = json.Unmarshal(jsonData, &specs)
	if err != nil {
		t.Fatalf("Failed to parse fuzzer.json: %v", err)
	}
	fuzzers := []string{}
	for _, spec := range specs {
		if spec.Host {
			fuzzers = append(fuzzers, spec.Targets...)
		}
	}

	// Create a empty file to act as a single input to each fuzzer.
	variants, err := filepath.Glob(path.Join(fxBuildDir, "host_*-fuzzer"))
	if err != nil {
		t.Fatalf("Failed to glob host fuzzer build directories: %v", err)
	}
	if len(variants) == 0 {
		t.SkipNow()
	}
	tempFile, err := ioutil.TempFile("", "empty.input")
	if err != nil {
		t.Fatalf("Failed to create empty input: %v", err)
	}
	tempName := tempFile.Name()
	tempFile.Close()
	defer os.Remove(tempName)

	// Check that each fuzzer runs and exits cleanly.
	for _, variant := range variants {
		for _, fuzzer := range fuzzers {
			cmd := exec.Command(path.Join(variant, fuzzer), tempName)
			output, err := cmd.CombinedOutput()
			if err != nil {
				t.Errorf("Failed to run %s: %v", fuzzer, err)
			}
			t.Log(string(output[:]))
		}
	}
}
