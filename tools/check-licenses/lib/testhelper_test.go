// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package testhelper provides helper utilities for tests.
package lib

import (
	"io/ioutil"
)

func getFileTree(config *Config) *FileTree {
	metrics := getMetrics()
	file_tree := NewFileTree(config, metrics)
	return file_tree
}

func getConfig() (*Config, error) {
	path := "config.json"
	json := `{"filesRegex":[],"skipFiles":[".gitignore"],"skipDirs":[".git"],"textExtensionList":["go"],"maxReadSize":6144,"separatorWidth":80,"outputFilePrefix":"NOTICE","outputFileExtension":"txt","product":"astro","singleLicenseFiles":["LICENSE"],"goldenLicenses":"golden","licensePatternDir":"golden/","baseDir":".","target":"all","logLevel":"verbose"}`
	if err := ioutil.WriteFile(path, []byte(json), 0644); err != nil {
		return nil, err
	}
	var config Config
	if err := config.Init(&path); err != nil {
		return nil, err
	}
	return &config, nil
}

func getMetrics() *Metrics {
	var metrics Metrics
	metrics.Init()
	return &metrics
}
