// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

package main

import (
	"flag"
	"os"
	"path/filepath"
	"testing"
)

var testDir = flag.String("test_data_dir", "", "The directory where test data reside")

func TestTestDir(t *testing.T) {
	if *testDir == "" {
		t.Fatalf("the required flag --test_data_dir=... was not supplied")
	}
}

func TestModelParsing(t *testing.T) {
	t.Parallel()
	tests := []string{
		"basic_time.yaml",
		"bti.yaml",
		"channel.yaml",
		"clock.yaml",
		"debuglog.yaml",
		"duration.yaml",
		"@nonymous_record_FFBFAD8A3BBD799586600B40A3453BBD95900F13.yaml",
		"index.yaml",
	}
	for _, fileName := range tests {
		fileName := fileName
		t.Run(fileName, func(t *testing.T) {
			fileName := filepath.Join(*testDir, "files", fileName)
			r, err := os.Open(fileName)
			if err != nil {
				t.Fatalf("could not open: %v: %v", fileName, err)
			}
			// For the time being, we're only interested in a successful parse.
			// Strict YAML parsing should be enough to alert us to possible
			// model mismatches.
			_, err = ParseYAML(r /*lenient=*/, false)
			if err != nil {
				t.Errorf("parse error for: %v: %v", fileName, err)
			}
		})
	}
}
