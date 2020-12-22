// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package golden

import (
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

var testDataDir = flag.String("test_data_dir", "", "Path to test data; only used in GN build")

func TestPatternsMatchExamples(t *testing.T) {
	wantDirs := []string{"examples", "patterns"}
	for _, folder := range wantDirs {
		p := filepath.Join(*testDataDir, folder)
		if _, err := os.Stat(p); os.IsNotExist(err) {
			t.Errorf("%v(%v doesn't exist): got %v, want %v", t.Name(), p, err, nil)
		}
	}
	examplesRoot := filepath.Join(*testDataDir, "examples")
	exampleFilesPath := []string{}
	err := filepath.Walk(examplesRoot,
		func(path string, info os.FileInfo, err error) error {
			if info.IsDir() {
				return nil
			}
			exampleFilesPath = append(exampleFilesPath, path)
			return nil
		})
	if err != nil {
		t.Errorf("%v, got %v", t.Name(), err)
	}

	patternsRoot := filepath.Join(*testDataDir, "patterns")
	patternFilesPath := []string{}
	err = filepath.Walk(patternsRoot,
		func(path string, info os.FileInfo, err error) error {
			if info.IsDir() {
				return nil
			}
			patternFilesPath = append(patternFilesPath, path)
			return nil
		})
	if err != nil {
		t.Errorf("%v, got %v", t.Name(), err)
	}

	if len(exampleFilesPath) != len(patternFilesPath) {
		t.Errorf("%v, got %v != %v", t.Name(), len(exampleFilesPath), len(patternFilesPath))
	}
	fmt.Printf("LENGTH %v", len(patternFilesPath))
	for _, patternFilePath := range patternFilesPath {
		patternFile, err := ioutil.ReadFile(patternFilePath)
		if err != nil {
			t.Errorf("%v, got %v", t.Name(), err)
		}
		exampleFilePath := strings.Replace(patternFilePath, ".lic", ".txt", -1)
		exampleFilePath = strings.Replace(exampleFilePath, "patterns", "examples", -1)
		exampleFile, err := ioutil.ReadFile(exampleFilePath)
		if err != nil {
			t.Errorf("%v, got %v", t.Name(), err)
		}
		regex := string(patternFile)
		// Update regex to ignore multiple white spaces, newlines, comments.
		// But first, trim whitespace away so we don't include unnecessary
		// comment syntax.
		regex = strings.Trim(regex, "\n ")
		regex = strings.ReplaceAll(regex, "\n", `[\s\\#\*\/]*`)
		regex = strings.ReplaceAll(regex, " ", `[\s\\#\*\/]*`)

		if !regexp.MustCompile(regex).Match(exampleFile) {
			t.Errorf("%v, %v pattern doesn't match example", t.Name(), patternFilePath)
		}
	}
}
