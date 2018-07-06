// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package source

import (
	"errors"
	"net/http"
	"os"
	"time"

	"amber/pkg"

	"fidl/fuchsia/amber"
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
	// A unique identifier that distinquishes this source from others.
	GetId() string

	// The config definition for this source.
	GetConfig() *amber.SourceConfig

	// TODO(etryzelaar) This is a bit of a hack, but the blob fetcher also
	// needs an authenticated http.Client. This really ought to be refactored.
	GetHttpClient() *http.Client

	// AvailableUpdates takes a list of packages and returns update metadata
	// for any updates available for those packages.
	AvailableUpdates(pkg []*pkg.Package) (map[pkg.Package]pkg.Package, error)

	// FetchUpdate retrieves the package content from this source, if it is available.
	// The package content is written to a file and an open File to the content
	// is returned, ready for reading.
	FetchPkg(pkg *pkg.Package) (*os.File, error)

	// CheckInterval is the time window during which at most CheckLimit() calls
	// are allowed
	CheckInterval() time.Duration

	// CheckLimit is the number of calls allowed per the unit of time specified in
	// CheckInterval
	CheckLimit() uint64

	// Equals should return true if the provide Source is the same as the receiver.
	Equals(s Source) bool

	// Write config to disk.
	Save() error

	// Delete config from disk, preventing the source from loading on service restart.
	DeleteConfig() error

	// Delete all state on disk, including the source config (if present).
	Delete() error

	// Log into the TUF remote server and return the oauth2 device flow
	// code to complete the authentication process.
	Login() (*amber.DeviceCode, error)

	// Close any resources we might have open.
	Close()
}
