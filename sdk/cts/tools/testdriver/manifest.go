// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testdriver

import (
	"encoding/json"
	"os"
)

// Manifest is used to store pertinent data about software versions under test
// and what tests are available to run.
type Manifest struct {
	// SDK: SDK struct; see `sdk.go`
	//
	// Required
	SDK SDK `json:"sdk"`

	// Tests: List of tests that are available to run.
	//
	// These are paths to individual tests relative to the `testdriver`
	// directory.
	//
	// Examples:
	//   prebuilt/hello-world
	//   compiled/foobar
	//
	// Required
	Tests []string `json:"tests"`
}

// NewManifest returns a Manifest instance that can be used to know what needs
// to be downloaded, built, and/or compiled.
func NewManifest(path string) (*Manifest, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	m := &Manifest{}
	d := json.NewDecoder(f)
	d.DisallowUnknownFields()
	if err = d.Decode(m); err != nil {
		return nil, err
	}

	return m, nil
}
