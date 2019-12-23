// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"bufio"
	"bytes"
	"fmt"
	"io"
	"os"
	"path/filepath"
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
		if err != nil {
			return nil, err
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

// Package loads the package descriptor from the package listed in the manifest and returns it.
func (m *Manifest) Package() (*pkg.Package, error) {
	f, err := os.Open(m.Paths["meta/package"])
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
			if len(strings.TrimSpace(line)) == 0 {
				return r, nil
			}
			err = nil
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

		// TODO(anmittal): make file comparision efficient.
		if duplicateSrc, ok := r[dest]; ok {
			if equal, err := filesEqual(src, duplicateSrc); err != nil {
				return r, err
			} else if !equal {
				return r, fmt.Errorf("build.parseManifest: Multiple entries for key, pointing to different files: %q, [%s, %s]", dest, src, duplicateSrc)
			}
			continue
		}
		r[dest] = src
	}
}

func filesEqual(file1, file2 string) (bool, error) {
	f1, err := os.Open(file1)
	if err != nil {
		return false, fmt.Errorf("error while opening file (%s): %s", file1, err)
	}
	defer f1.Close()
	f1s, err := f1.Stat()
	if err != nil {
		return false, fmt.Errorf("error while file stat (%s): %s", file1, err)
	}

	f2, err := os.Open(file2)
	if err != nil {
		return false, fmt.Errorf("error while opening file (%s): %s", file2, err)
	}
	defer f2.Close()
	f2s, err := f2.Stat()
	if err != nil {
		return false, fmt.Errorf("error while file stat (%s): %s", file2, err)
	}

	if os.SameFile(f1s, f2s) {
		return true, nil
	}
	if f1s.Size() != f2s.Size() {
		return false, nil
	}
	size := f1s.Size()
	chunkSize := int64(1024 * 16)
	b1 := make([]byte, chunkSize)
	b2 := make([]byte, chunkSize)
	offset := int64(0)
	for {
		nextChunkSize := chunkSize
		if size-offset < nextChunkSize {
			nextChunkSize = size - offset
		}
		n1, err1 := io.ReadFull(f1, b1[0:nextChunkSize])
		if err1 != nil && err1 != io.EOF {
			return false, fmt.Errorf("error while reading file (%s): %s", file1, err1)
		}

		n2, err2 := io.ReadFull(f2, b2[0:nextChunkSize])
		if err2 != nil && err2 != io.EOF {
			return false, fmt.Errorf("error while reading file (%s): %s", file2, err2)
		}
		if n1 != n2 {
			return false, fmt.Errorf("something wrong, read size should have been same for files %q and %q", file1, file2)
		}
		if n1 == 0 {
			return true, nil
		}
		offset += int64(n1)
		if !bytes.Equal(b1[0:n1], b2[0:n2]) {
			return false, nil
		}
	}
}
