// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"io/ioutil"
	"regexp"
	"testing"
)

func TestConfigInit(t *testing.T) {
	path := "config.json"
	json := `{"filesRegex":[],"skipFiles":[".gitignore"],"skipDirs":[".git"],"textExtensionList":["go"],"maxReadSize":6144,"separatorWidth":80,"outputFilePrefix":"NOTICE","outputFileExtension":"txt","product":"astro","singleLicenseFiles":["LICENSE"],"goldenLicenses":"golden","licensePatternDir":"golden/","baseDir":".","target":"all","logLevel":"verbose"}`
	if err := ioutil.WriteFile(path, []byte(json), 0644); err != nil {
		t.Errorf("%v(): got %v", t.Name(), err)
	}
	var config Config
	if err := config.Init(&path); err != nil {
		t.Errorf("%v(): got %v", t.Name(), err)
	}
	want := "."
	if config.BaseDir != want {
		t.Errorf("%v(): got %v, want %v", t.Name(), config.BaseDir, want)
	}
}

func TestFileIsSingleLicense(t *testing.T) {
	funcName := "TestFileIsSingleLicense"
	name := "LICENSE-THIRD-PARTY"
	singleLicenseFiles := []string{"LICENSE", "README"}
	if !isSingleLicenseFile(name, singleLicenseFiles) {
		t.Errorf("%v: %v is not a single license file", funcName, name)
	}
}

func TestLicenseAppend(t *testing.T) {
	funcName := "TestLicenseAppend"
	license := License{
		pattern:  regexp.MustCompile("abcdefghijklmnopqrs\ntuvwxyz"),
		category: "alphabet-test",
	}
	want := 0
	if len(license.matches) != want {
		t.Errorf("%v(): got %v, want %v", funcName, len(license.matches), want)
	}
	license.append("test_path_0")
	want = 1
	if len(license.matches) != want {
		t.Errorf("%v(): got %v, want %v", funcName, len(license.matches), want)
	}
	if len(license.matches[0].files) != want {
		t.Errorf("%v(): got %v, want %v", funcName, len(license.matches[0].files), want)
	}
}

func TestMetricsInit(t *testing.T) {
	funcName := "TestMetricsInit"
	var metrics Metrics
	metrics.Init()
	num_values := len(metrics.values)
	num_order := len(metrics.order)
	want := 0
	if num_values == want {
		t.Errorf("%v(): got %v, want %v", funcName, num_values, want)
	}
	if num_values != num_order {
		t.Errorf("%v(): got %v, want %v", funcName, num_values, num_order)
	}
}
