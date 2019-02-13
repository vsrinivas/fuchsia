// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testrunner

import (
	"encoding/json"
	"fmt"
	"io/ioutil"

	"fuchsia.googlesource.com/tools/testsharder"
)

// LoadTests loads the list of tests from the given path.
func LoadTests(path string) ([]testsharder.Test, error) {
	bytes, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read %q: %v", path, err)
	}

	var tests []testsharder.Test
	if err := json.Unmarshal(bytes, &tests); err != nil {
		return nil, fmt.Errorf("failed to unmarshal %q: %v", path, err)
	}

	return tests, nil
}
