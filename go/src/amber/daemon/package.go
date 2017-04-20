// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"fmt"
	"os"
)

// Package represents a package installed or desired to be installed
type Package struct {
	Name    string
	Version string
}

func (p *Package) String() string {
	return fmt.Sprintf("Package[name:'%s', version:'%s']", p.Name, p.Version)
}

// ReadPkgUpdate will return the bytes for a Package from the Daemon's internal
// store. NOTE: Currently not implemented.
func ReadPkgUpdate(update string) *os.File {
	return nil
}
