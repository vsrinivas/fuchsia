// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gndoc

import (
	"encoding/json"
	"fmt"
	"io"
	"path/filepath"
	"strings"
)

type SourceMap map[string]string

func NewSourceMap(input io.Reader) (*SourceMap, error) {
	s := SourceMap(make(map[string]string))
	err := s.parseJson(input)
	if err != nil {
		return nil, err
	}
	return &s, nil
}

// parseJson extracts the source project information into the map.
func (s SourceMap) parseJson(input io.Reader) error {
	// Decode the json into the map.
	var sources []map[string]interface{}
	decoder := json.NewDecoder(input)
	if err := decoder.Decode(&sources); err != nil {
		return err
	}

	for _, source := range sources {
		if revision, ok := source["revision"]; ok {
			s[source["name"].(string)] = fmt.Sprintf("%v/+/%s", source["remote"], revision)
		} else {
			s[source["name"].(string)] = fmt.Sprintf("%v/+/master", source["remote"])
		}
	}

	return nil
}

// GetSourceLink returns a URL based on a file and a line number.
func (s SourceMap) GetSourceLink(file string, line int) string {
	// We need to trim off first slashes in the filename.
	file = strings.TrimPrefix(file, "//")
	project := file
	for {
		if _, exists := s[project]; exists {
			break
		}
		project = filepath.Dir(project)
		if project == "." {
			return ""
		}
	}
	if line == 0 {
		return fmt.Sprintf("%s%s", s[project], file[len(project):])
	}
	return fmt.Sprintf("%s%s#%d", s[project], file[len(project):], line)
}
