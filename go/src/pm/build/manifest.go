// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"encoding/json"

	"fuchsia.googlesource.com/pm/ignores"
	"fuchsia.googlesource.com/pm/pkg"
)

// Manifest describes the list of files that are to become the contents of a package
type Manifest struct {
	// Srcs is a set of manifests and/or directories that are the contents of the package
	Srcs []string
	// Paths is the fully computed contents of a package in the form of "destination": "source"
	Paths map[string]string
}

// NewManifest initializes a manifest from the given paths. If a path is a
// directory, it is globbed and the manifest includes all unignored files under
// that directory. If the path is a manifest file, the file is parsed and all
// files are mapped as described by the manifest file. Manifest files contain
// lines with "destination=source". Lines that do not match this pattern are
// ignored.
func NewManifest(paths []string) (*Manifest, error) {
	m := &Manifest{
		Srcs:  paths,
		Paths: make(map[string]string),
	}

	for _, path := range paths {
		info, err := os.Stat(path)
		if err != nil {
			return nil, err
		}

		var newPaths map[string]string
		if info.IsDir() {
			newPaths, err = walk(path)
		} else {
			newPaths, err = parseManifest(path)
		}
		for k, v := range newPaths {
			m.Paths[k] = v
		}
	}

	return m, nil
}

// Meta provides the list of files from the manifest that are to be included in
// meta.far.
func (m *Manifest) Meta() map[string]string {
	meta := map[string]string{}
	for d, s := range m.Paths {
		// TODO(raggi): portable separator concerns
		if strings.HasPrefix(d, "meta/") {
			meta[d] = s
		}
	}
	return meta
}

// Package loads the package descriptor from the package.json listed in the manifest and returns it.
func (m *Manifest) Package() (*pkg.Package, error) {
	f, err := os.Open(m.Paths["meta/package.json"])
	if err != nil {
		return nil, fmt.Errorf("build.Manifest.Package: %s", err)
	}
	defer f.Close()
	var p pkg.Package
	return &p, json.NewDecoder(f).Decode(&p)
}

// Content returns the list of files from the manifest that are not to be
// included in the meta.far.
func (m *Manifest) Content() map[string]string {
	content := map[string]string{}
	for d, s := range m.Paths {
		// TODO(raggi): portable separator concerns
		if !strings.HasPrefix(d, "meta/") {
			content[d] = s
		}
	}
	return content
}

// SigningFiles returns a sorted list of files from Meta() except for meta/signature.
func (m *Manifest) SigningFiles() []string {
	metas := m.Meta()
	signingFiles := make([]string, 0, len(metas))
	for dest := range metas {
		if dest == "meta/signature" {
			continue
		}
		signingFiles = append(signingFiles, dest)
	}

	sort.Strings(signingFiles)
	return signingFiles
}

func walk(root string) (map[string]string, error) {
	r := map[string]string{}

	err := filepath.Walk(root, func(source string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		dest, err := filepath.Rel(root, source)
		if err != nil {
			return err
		}
		if ignores.Match(dest) {
			return filepath.SkipDir
		}

		if info.IsDir() {
			return nil
		}

		r[dest] = source

		return nil
	})

	return r, err
}

func parseManifest(path string) (map[string]string, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("build.parseManifest: %s", err)
	}
	defer f.Close()
	r := map[string]string{}
	b := bufio.NewReader(f)
	for {
		line, err := b.ReadString('\n')
		if err == io.EOF {
			return r, nil
		}
		if err != nil {
			return r, err
		}

		parts := strings.SplitN(line, "=", 2)
		if len(parts) < 2 {
			continue
		}
		src := strings.TrimSpace(parts[1])
		dest := strings.TrimSpace(parts[0])

		r[dest] = src
	}
}
