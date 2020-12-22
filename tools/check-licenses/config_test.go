// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"flag"
	"io/ioutil"
	"path/filepath"
	"testing"
)

var testDataDir = flag.String("test_data_dir", "", "Path to test data; only used in GN build")

func TestConfigNew(t *testing.T) {
	folder := t.TempDir()
	path := filepath.Join(folder, "config.json")
	json := `{"filesRegex":[],"skipFiles":[".gitignore"],"skipDirs":[".git"],"textExtensionList":["go"],"maxReadSize":6144,"separatorWidth":80,"outputFilePrefix":"NOTICE","outputFileExtension":"txt","outputLicenseFile": true,"product":"astro","singleLicenseFiles":["LICENSE"],"licensePatternDir":"golden/","baseDir":".","target":"all","logLevel":"verbose", "customProjectLicenses": [{"projectRoot": "test", "licenseLocation": "test"}], "exitOnUnlicensedFiles": false}`
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
	p := filepath.Join(*testDataDir, "config", "config.json")
	_, err := NewConfig(p)
	if err != nil {
		t.Errorf("%v(): got %v", t.Name(), err)
	}
}
