// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"context"
	"io/ioutil"
	"path/filepath"
	"testing"
)

func TestFileTreeNew(t *testing.T) {
	folder := mkDir(t)
	path := filepath.Join(folder, "config.json")
	json := `{"filesRegex":[],"skipFiles":[".gitignore"],"skipDirs":[".git"],"textExtensionList":["go"],"maxReadSize":6144,"separatorWidth":80,"outputFilePrefix":"NOTICE","outputFileExtension":"txt","product":"astro","singleLicenseFiles":["LICENSE"],"licensePatternDir":"golden/","baseDir":".","target":"all","logLevel":"verbose", "customProjectLicenses": [{"projectRoot": "test", "licenseLocation": "test"}]}`
	if err := ioutil.WriteFile(path, []byte(json), 0644); err != nil {
		t.Fatal(err)
	}
	config := Config{}
	if err := config.Init(path); err != nil {
		t.Fatal(err)
	}
	metrics := Metrics{}
	metrics.Init()
	config.BaseDir = folder
	if NewFileTree(context.Background(), config.BaseDir, nil, &config, &metrics) == nil {
		t.Errorf("%v(): got %v, want %v", t.Name(), nil, "*FileTree")
	}
}

func TestHasLowerPrefix(t *testing.T) {
	name := "LICENSE-THIRD-PARTY"
	singleLicenseFiles := []string{"license", "readme"}
	if !hasLowerPrefix(name, singleLicenseFiles) {
		t.Errorf("%v: %v is not a single license file", t.Name(), name)
	}
}
