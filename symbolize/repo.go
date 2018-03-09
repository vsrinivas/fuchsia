// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"bufio"
	"os"
	"strings"
)

// SymbolizerRepo keeps track of build objects and source files used in those build objects.
type SymbolizerRepo struct {
	builds map[string]string
}

func (s *SymbolizerRepo) AddObject(build, file string) {
	s.builds[build] = file
}

func NewRepo() *SymbolizerRepo {
	return &SymbolizerRepo{
		builds: make(map[string]string),
	}
}

// NewRepoFromFile generates a SymbolizerRepo from an ids.txt file.
func (s *SymbolizerRepo) AddObjectsFromIdsFile(filepath string) error {
	file, err := os.Open(filepath)
	if err != nil {
		return err
	}
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		parts := strings.SplitN(scanner.Text(), " ", 2)
		s.AddObject(parts[0], parts[1])
	}
	return nil
}

// NewRepo builds a SymbolizerRepo from a map that has buildids as keys and objects as values.
func (s *SymbolizerRepo) AddObjects(builds map[string]string) {
	for build, file := range builds {
		s.AddObject(build, file)
	}
}

// GetBuildObject lets you get the build object associated with a build id.
func (s *SymbolizerRepo) GetBuildObject(buildid string) string {
	return s.builds[buildid]
}
