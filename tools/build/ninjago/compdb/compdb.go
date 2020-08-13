// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package compdb

import (
	"encoding/json"
	"io"
)

// Command specifies one way a translation unit is compiled in the project.
type Command struct {
	// The working directory of the compilation.
	Directory string `json:"directory"`

	// The main translation unit source processed by this compilation step.
	File string `json:"file"`

	// The compile command executed.
	Command string `json:"command"`

	// The compile command executed as list of strings.
	Arguments []string `json:"arguments"`

	// The name of the output created by this compilation step.
	Output string `json:"output"`
}

// Parse parses the compilation database.
func Parse(file io.Reader) ([]Command, error) {
	var compdb []Command
	if err := json.NewDecoder(file).Decode(&compdb); err != nil {
		return nil, err
	}
	return compdb, nil
}
