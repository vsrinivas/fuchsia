// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"errors"
	"fmt"
	"io"
	"strings"
)

// depfile writes Ninja dep files.
type depfile struct {
	// outputPath is the path to some new filesystem entity.
	outputPath string

	// inputPaths are the sources that were used to generate outputPath.
	inputPaths []string
}

// WriteTo writes this depfile to the given io.Writer. Returns an error if the output is
// empty, there are no input paths, or any of the inputs is empty.
func (df *depfile) WriteTo(w io.Writer) (int64, error) {
	if df.outputPath == "" {
		return 0, errors.New("depfile is missing output")
	}
	if len(df.inputPaths) == 0 {
		return 0, errors.New("depfile is missing inputs")
	}
	for _, input := range df.inputPaths {
		if input == "" {
			return 0, fmt.Errorf("got empty dep file input path: %v", df.inputPaths)
		}
	}

	contents := fmt.Sprintf("%s: %s\n", df.outputPath, strings.Join(df.inputPaths, " "))
	n, err := w.Write([]byte(contents))
	return int64(n), err
}
