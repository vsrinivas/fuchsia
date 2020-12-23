// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"io/ioutil"
	"path/filepath"
	"testing"

	checklicenses "go.fuchsia.dev/fuchsia/tools/check-licenses"
)

func TestMainLoadConfig(t *testing.T) {
	path := filepath.Join(t.TempDir(), "config.json")
	json := `{"skipFiles":[".gitignore"],"skipDirs":[".git"],"textExtensionList":["go"],"maxReadSize":6144,"separatorWidth":80,"outputFilePrefix":"NOTICE","outputFileExtension":"txt","outputLicenseFile": true, "product":"astro","singleLicenseFiles":["LICENSE"],"licensePatternDir":"golden/","baseDir":".","target":"all","logLevel":"verbose", "customProjectLicenses": [{"projectRoot": "test", "licenseLocation": "test"}], "prohibitedLicenseTypes": ["gnu"], "exitOnProhibitedLicenseTypes": false, "exitOnUnlicensedFiles": false}`
	if err := ioutil.WriteFile(path, []byte(json), 0o600); err != nil {
		t.Errorf("%v(): got %v", t.Name(), err)
	}
	_, err := checklicenses.NewConfig(path)
	if err != nil {
		t.Errorf("%v(): got %v", t.Name(), err)
	}
}
