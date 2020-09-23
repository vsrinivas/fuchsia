// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"io/ioutil"
	"path/filepath"
	"testing"
)

func TestTemplateShouldCompress(t *testing.T) {
	path := filepath.Join(mkDir(t), "config.json")
	json := `{"filesRegex":[],"skipFiles":[".gitignore"],"skipDirs":[".git"],"textExtensionList":["go"],"maxReadSize":6144,"separatorWidth":80,"outputFilePrefix":"NOTICE","outputFileExtension":"txt","product":"astro","singleLicenseFiles":["LICENSE"],"goldenLicenses":"golden","licensePatternDir":"golden/","baseDir":".","target":"all","logLevel":"verbose"}`
	if err := ioutil.WriteFile(path, []byte(json), 0644); err != nil {
		t.Errorf("%v(): got %v", t.Name(), err)
	}
	var config Config
	if err := config.Init(&path); err != nil {
		t.Errorf("%v(): got %v", t.Name(), err)
	}
	var got, want bool
	config.OutputFileExtension = "html.gz"
	got = shouldCompressOutputFile(&config)
	want = true
	if got != want {
		t.Errorf("%v(): got %v, want %v", t.Name(), got, want)
	}
	config.OutputFileExtension = "html"
	got = shouldCompressOutputFile(&config)
	want = false
	if got != want {
		t.Errorf("%v(): got %v, want %v", t.Name(), got, want)
	}
}

func TestTemplateValidCompression(t *testing.T) {
	// TODO(http://fxbug.dev/56847): Implement.
}
