// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"bufio"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"fuchsia.googlesource.com/pm/ignores"
)

// Manifest describes the list of files that are to become the contents of a package
type Manifest struct {
	// Path is the manifest source path provided by a user, it may be either a directory or a manifest file
	Path string
	// Paths is the fully computed contents of a package in the form of "destination": "source"
	Paths map[string]string
}

// NewManifest initializes a manifest from the given path. If path is a
// directory, it is globbed and the manifest includes all unignored files under
// that directory. If the path is a manifest file, the file is parsed and all
// files are mapped as described by the manifest file. Manifest files contain
// lines with "destination=source". Lines that do not match this pattern are
// ignored.
func NewManifest(path string) (*Manifest, error) {
	info, err := os.Stat(path)
	if err != nil {
		return nil, err
	}

	m := &Manifest{
		Path: path,
	}
	if info.IsDir() {
		m.Paths, err = walk(path)
	} else {
		m.Paths, err = parseManifest(path)
	}

	return m, err
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
		return nil, err
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
