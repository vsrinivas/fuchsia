// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config

import (
	"flag"
	"io/ioutil"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/host_target_testing/artifacts"
)

type ArchiveConfig struct {
	outputDir     string
	lkgbPath      string
	artifactsPath string
	archive       *artifacts.Archive
}

func NewArchiveConfig(fs *flag.FlagSet) *ArchiveConfig {
	c := &ArchiveConfig{}

	testDataPath := filepath.Join(filepath.Dir(os.Args[0]), "test_data", "system-tests")

	fs.StringVar(&c.outputDir, "output-dir", "", "save temporary files to this directory, defaults to a tempdir")
	fs.StringVar(&c.lkgbPath, "lkgb", filepath.Join(testDataPath, "lkgb"), "path to lkgb, default is $FUCHSIA_DIR/prebuilt/tools/lkgb/lkgb")
	fs.StringVar(&c.artifactsPath, "artifacts", filepath.Join(testDataPath, "artifacts"), "path to the artifacts binary, default is $FUCHSIA_DIR/prebuilt/tools/artifacts/artifacts")

	return c
}

func (c *ArchiveConfig) OutputDir() (string, func(), error) {
	// If we specified an -output-dir, return it, and a cleanup function
	// that does nothing.
	if c.outputDir != "" {
		return c.outputDir, func() {}, nil
	}

	// Otherwise create a tempdir, and return a cleanup function that
	// deletes the tempdir when called.
	outputDir, err := ioutil.TempDir("", "system-tests")
	if err != nil {
		return "", func() {}, err
	}

	return outputDir, func() { os.RemoveAll(outputDir) }, nil
}

func (c *ArchiveConfig) BuildArchive() *artifacts.Archive {
	if c.archive == nil {
		// Connect to the build archive service.
		c.archive = artifacts.NewArchive(c.lkgbPath, c.artifactsPath)
	}

	return c.archive
}
