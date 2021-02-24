// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"encoding/json"
	"flag"
	"io/ioutil"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
)

var testDataDir = flag.String("test_data_dir", "testdata", "Path to test data; only used in GN build")

func TestConfigNew(t *testing.T) {
	folder := t.TempDir()
	path := filepath.Join(folder, "config.json")
	json := `{"skipFiles":[".gitignore"],"skipDirs":[".git"],"textExtensionList":["go"],"maxReadSize":6144,"outputFilePrefix":"NOTICE","outputFileExtensions":["txt"],"outputLicenseFile": true,"singleLicenseFiles":["LICENSE"],"licensePatternDir":"golden/","baseDir":".","target":"all","logLevel":"verbose", "customProjectLicenses": [{"projectRoot": "test", "licenseLocation": "test"}], "exitOnUnlicensedFiles": false}`
	if err := ioutil.WriteFile(path, []byte(json), 0o600); err != nil {
		t.Errorf("%v(): got %v", t.Name(), err)
	}
	config, err := NewConfig(path)
	if err != nil {
		t.Errorf("%v(): got %v", t.Name(), err)
	}
	want := "."
	if config.BaseDir != want {
		t.Errorf("%v(): got %v, want %v", t.Name(), config.BaseDir, want)
	}
}

func TestConfigDefault(t *testing.T) {
	p := filepath.Join(*testDataDir, "config", "new", "config.json")
	_, err := NewConfig(p)
	if err != nil {
		t.Errorf("%v(): got %v", t.Name(), err)
	}
}

func TestConfigMerge(t *testing.T) {
	// Find the right testdata directory for this test.
	testDir, err := filepath.Abs(filepath.Join(*testDataDir, "config", "merge"))
	if err != nil {
		t.Fatal(err)
	}

	// Read the want.json file into a string.
	wantJsonPath := filepath.Join(testDir, "want.json")
	b, err := ioutil.ReadFile(wantJsonPath)
	if err != nil {
		t.Fatal(err)
	}
	wantJson := string(b)

	// Create a config object from the want.json file.
	d := json.NewDecoder(strings.NewReader(wantJson))
	d.DisallowUnknownFields()
	var expected Config
	if err := d.Decode(&expected); err != nil {
		t.Fatal(err)
	}

	left, err := NewConfig(filepath.Join(testDir, "merge_left.json"))
	if err != nil {
		t.Fatal(err)
	}
	right, err := NewConfig(filepath.Join(testDir, "merge_right.json"))
	if err != nil {
		t.Fatal(err)
	}
	left.Merge(right)

	if !reflect.DeepEqual(*left, expected) {
		t.Fatalf("got: %v, want:%v\n", *left, expected)
	}
}
