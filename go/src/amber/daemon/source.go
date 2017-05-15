// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"errors"
	"os"
	"time"
)

// ErrNoUpdate is returned if no update is available
var ErrNoUpdate = errors.New("amber/source: no update available")

// ErrUnknownPkg is returned if the Source doesn't have any data about any
// version of the package.
var ErrUnknownPkg = errors.New("amber/source: package not known")

// ErrNoUpdateContent is returned if the requested package content couldn't be
// retrieved.
var ErrNoUpdateContent = errors.New("amber/source: update content not available")

// Source provides a way to get information about a package update and a way
// to get that update.
type Source interface {
	// FetchUpdate gets the package metadata of an available version that is newer
	// than the passed in Package descriptor
	FetchUpdate(pkg *Package) (*Package, error)
	// FetchPkg retrieves the package content from this source, if it is available.
	// The package content is written to a file and an open File to the content
	// is returned, ready for reading.
	FetchPkg(pkg *Package) (*os.File, error)

	// CheckInterval is minimum time between polls of this source
	CheckInterval() time.Duration

	// Equals should return true if the provide Source is the same as the receiver.
	Equals(s Source) bool
}
