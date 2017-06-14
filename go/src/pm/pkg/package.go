// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package pkg contains the in memory representations of a Package in the pm
// system and associated utilities.
package pkg

// Package is a representation of basic package metadata
type Package struct {
	Name    string `json:"name"`
	Version string `json:"version"`
}
