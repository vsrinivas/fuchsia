// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"flag"
	"io/ioutil"
	"os"
	"path/filepath"

	"golang.org/x/crypto/ed25519"
)

// Config contains global build configuration for other build commands
type Config struct {
	OutputDir    string
	ManifestPath string
	KeyPath      string
	TempDir      string
	PkgName      string

	// the manifest is memoized lazily, on the first call to Manifest()
	manifest *Manifest
}

// NewConfig initializes a new configuration with conventional defaults
func NewConfig() *Config {
	cfg := &Config{
		OutputDir:    ".",
		ManifestPath: ".",
		KeyPath:      "",
		TempDir:      os.TempDir(),
		PkgName:      "",
	}
	return cfg
}

// TestConfig produces a configuration suitable for testing. It creates a
// temporary directory as a parent of the returned config.OutputDir and
// config.TempDir. Callers should remove this directory.
func TestConfig() *Config {
	d, err := ioutil.TempDir("", "pm-test")
	if err != nil {
		panic(err)
	}
	cfg := &Config{
		OutputDir:    filepath.Join(d, "output"),
		ManifestPath: filepath.Join(d, "manifest"),
		KeyPath:      filepath.Join(d, "key"),
		TempDir:      filepath.Join(d, "tmp"),
		PkgName:      filepath.Join(d, "pkg"),
	}
	for _, d := range []string{cfg.OutputDir, cfg.TempDir} {
		os.MkdirAll(d, os.ModePerm)
	}
	return cfg
}

// InitFlags adds flags to a flagset for altering Config defaults
func (c *Config) InitFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.OutputDir, "o", c.OutputDir, "archive output directory")
	fs.StringVar(&c.ManifestPath, "m", c.ManifestPath, "build manifest (or package directory)")
	fs.StringVar(&c.KeyPath, "k", c.KeyPath, "signing key")
	fs.StringVar(&c.TempDir, "t", c.TempDir, "temporary directory")
	fs.StringVar(&c.PkgName, "n", c.PkgName, "name of the packages")
}

// PrivateKey loads the configured private key
func (c *Config) PrivateKey() (ed25519.PrivateKey, error) {
	buf, err := ioutil.ReadFile(c.KeyPath)
	if err != nil {
		return nil, err
	}
	return ed25519.PrivateKey(buf), nil
}

// Manifest initializes and returns the configured manifest. The manifest may be
// modified during the build process to add/remove files.
func (c *Config) Manifest() (*Manifest, error) {
	var err error
	if c.manifest == nil {
		sources := []string{}

		if c.ManifestPath != "" {
			sources = append(sources, c.ManifestPath)
		}

		if c.OutputDir != "" {
			sources = append(sources, c.OutputDir)
		}

		if len(sources) == 0 {
			err = os.ErrNotExist
		}
		c.manifest, err = NewManifest(sources)
	}
	return c.manifest, err
}
