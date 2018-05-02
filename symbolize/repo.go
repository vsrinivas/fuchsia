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
	"sync"

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

type buildInfo struct {
	filepath string
	buildID  string
}

// SymbolizerRepo keeps track of build objects and source files used in those build objects.
type SymbolizerRepo struct {
	lock    sync.RWMutex
	sources []Source
	builds  map[string]*buildInfo
}

func (s *SymbolizerRepo) AddObject(build, filename string) {
	s.lock.Lock()
	defer s.lock.Unlock()
	s.builds[build] = &buildInfo{
		filepath: filename,
		buildID:  build,
	}
}

func NewRepo() *SymbolizerRepo {
	return &SymbolizerRepo{
		builds: make(map[string]*buildInfo),
	}
}

func (s *SymbolizerRepo) loadSource(source Source) error {
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

// AddSource adds a source of binaries and all contained binaries.
func (s *SymbolizerRepo) AddSource(source Source) error {
	s.sources = append(s.sources, source)
	return s.loadSource(source)
}

func (s *SymbolizerRepo) reloadSources() error {
	for _, source := range s.sources {
		if err := s.loadSource(source); err != nil {
			return err
		}
	}
	return nil
}

func (s *SymbolizerRepo) readInfo(buildid string) (*buildInfo, bool) {
	s.lock.RLock()
	info, ok := s.builds[buildid]
	s.lock.RUnlock()
	return info, ok
}

// GetBuildObject lets you get the build object associated with a build id.
func (s *SymbolizerRepo) GetBuildObject(buildid string) (string, error) {
	// No defer is used here because we don't want to hold the read lock
	// for very long.
	info, ok := s.readInfo(buildid)
	if !ok {
		// If we don't recognize that build id, reload all sources.
		s.reloadSources()
		info, ok = s.readInfo(buildid)
		if !ok {
			// If we still don't know about that build, return an error.
			return "", fmt.Errorf("unrecognized buildid %s", buildid)
		}
	}
	return info.filepath, nil
}
