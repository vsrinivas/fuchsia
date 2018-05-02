// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"bufio"
	"bytes"
	"encoding/hex"
	"fmt"
	"os"
	"strings"

	"fuchsia.googlesource.com/tools/elflib"
)

type Binary struct {
	BuildID string
	Name    string
}

type Source interface {
	GetBinaries() ([]Binary, error)
}

type buildIDError struct {
	err      error
	filename string
}

func newBuildIDError(err error, filename string) *buildIDError {
	return &buildIDError{err: err, filename: filename}
}

func (b buildIDError) Error() string {
	return fmt.Sprintf("error reading %s: %v", b.filename, b.err)
}

func VerifyBinary(filename, build string) error {
	file, err := os.Open(filename)
	if err != nil {
		return newBuildIDError(err, filename)
	}
	buildIDs, err := elflib.GetBuildIDs(filename, file)
	if err != nil {
		return newBuildIDError(err, filename)
	}
	binBuild, err := hex.DecodeString(build)
	if err != nil {
		return newBuildIDError(fmt.Errorf("build id `%s` is not a hex string: %v", build, err), filename)
	}
	for _, buildID := range buildIDs {
		if bytes.Equal(buildID, binBuild) {
			return nil
		}
	}
	return newBuildIDError(fmt.Errorf("build id `%s` could not be found", build), filename)
}

type IDsSource struct {
	pathToIDs string
}

func NewIDsSource(pathToIDs string) Source {
	return &IDsSource{pathToIDs}
}

func (i *IDsSource) GetBinaries() ([]Binary, error) {
	file, err := os.Open(i.pathToIDs)
	if err != nil {
		return nil, err
	}
	scanner := bufio.NewScanner(file)
	out := []Binary{}
	for line := 0; scanner.Scan(); line++ {
		parts := strings.SplitN(scanner.Text(), " ", 2)
		if len(parts) != 2 {
			return nil, fmt.Errorf("error parsing %s at line %d", i.pathToIDs, line)
		}
		build := parts[0]
		filename := parts[1]
		if err := VerifyBinary(filename, build); err != nil {
			return nil, err
		}
		out = append(out, Binary{Name: filename, BuildID: build})
	}
	return out, nil
}

// SymbolizerRepo keeps track of build objects and source files used in those build objects.
type SymbolizerRepo struct {
	builds map[string]string
}

func (s *SymbolizerRepo) AddObject(build, filename string) {
	s.builds[build] = filename
}

func NewRepo() *SymbolizerRepo {
	return &SymbolizerRepo{
		builds: make(map[string]string),
	}
}

// AddSource adds a source of binaries and all contained binaries.
func (s *SymbolizerRepo) AddSource(source Source) error {
	// TODO: Add source to list of sources so that it can be reloaded
	bins, err := source.GetBinaries()
	if err != nil {
		return err
	}
	// TODO: Do this in parallel
	for _, bin := range bins {
		s.AddObject(bin.BuildID, bin.Name)
	}
	return nil
}

// GetBuildObject lets you get the build object associated with a build id.
func (s *SymbolizerRepo) GetBuildObject(buildid string) (string, bool) {
	// TODO: reload all ids.txt if buildid was not found. This requires locking something.
	file, ok := s.builds[buildid]
	return file, ok
}
