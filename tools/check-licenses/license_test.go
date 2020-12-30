// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"encoding/json"
	"io/ioutil"
	"path/filepath"
	"regexp"
	"strconv"
	"testing"
)

// Successfully create a License object from a very simple license pattern.
func TestLicenseNew(t *testing.T) {
	want, got := setupLicenseTest("simple", t)
	if !got.Equal(want) {
		t.Errorf("%v(): got %v, want %v", t.Name(), got, want)
	}
}

// Successfully create a License object from a very simple license pattern,
// converting all empty space characters into "[\s\\#\*\/]*"
func TestLicenseNewWithSpaces(t *testing.T) {
	want, got := setupLicenseTest("spaces", t)
	if !got.Equal(want) {
		t.Errorf("%v(): got %v, want %v", t.Name(), got, want)
	}
}

// Successfully create a prohibited License object from a very simple license pattern.
func TestLicenseNewProhibited(t *testing.T) {
	want, got := setupLicenseTest("prohibited", t)
	if !got.Equal(want) {
		t.Errorf("%v(): got %v, want %v", t.Name(), got, want)
	}
}

func TestLicenseGetAuthorMatches(t *testing.T) {
	data := []string{
		"Copyright (C) 2020 Foo All rights reserved",
		"Copyright © 2020 Foo All rights reserved",
		"Copyright © 2020 Foo",
	}
	for _, in := range data {
		if m := getAuthorMatches([]byte(in)); len(m) == 0 {
			t.Errorf("%q failed, got %q", in, m)
		}
	}
}

func setupLicenseTest(name string, t *testing.T) (*License, *License) {
	// Find the location of the test data files.
	testDir, err := filepath.Abs(filepath.Join(*testDataDir, "license", name))
	if err != nil {
		t.Fatal(err)
	}

	// Read the want.json file into a string.
	wantJsonPath := filepath.Join(testDir, "want.json")
	b, err := ioutil.ReadFile(wantJsonPath)
	if err != nil {
		t.Fatal(err)
	}

	// The License pattern field (regexp) does not support JSON unmarshalling,
	// so we can't decode want.json into a License struct directly.
	// Instead, we read the json blob into a map and create the "want" struct manually.
	var jsonMap map[string]string
	err = json.Unmarshal(b, &jsonMap)
	if err != nil {
		t.Fatal(err)
	}

	re, err := regexp.Compile(jsonMap["pattern"])
	if err != nil {
		t.Fatal(err)
	}

	valid, err := strconv.ParseBool(jsonMap["valid license"])
	if err != nil {
		t.Fatal(err)
	}

	want := &License{
		Category:  jsonMap["category"],
		ValidType: valid,
		pattern:   re,
		matches:   map[string]*Match{},
	}

	// Initialize a test config for calling NewLicense.
	configPath := filepath.Join(*testDataDir, "license", name, "config.json")
	config, err := NewConfig(configPath)
	if err != nil {
		t.Fatal(err)
	}

	licPath := filepath.Join(testDir, "patterns", "example.lic")
	got, err := NewLicense(licPath, config)
	if err != nil {
		t.Fatal(err)
	}

	return want, got
}
