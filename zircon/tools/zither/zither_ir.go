// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package zither contains abstractions and utilities shared by the various backends.
package zither

import (
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// Summary is a summarized representation of a FIDL library's set of
// declarations. It contains the minimal subset of information needed for the
// zither backends in a maximally convenient form.
type Summary struct {
	Name fidlgen.LibraryName
}

// NewSummary creates a Summary from FIDL IR.
func NewSummary(ir *fidlgen.Root) (*Summary, error) {
	name, err := fidlgen.ReadLibraryName(string(ir.Name))
	if err != nil {
		return nil, err
	}

	return &Summary{Name: name}, nil
}
