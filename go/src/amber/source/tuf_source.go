// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package source

import (
	"errors"
	"io/ioutil"
	"os"
	"time"

	"amber/lg"
	"amber/pkg"

	"github.com/flynn/go-tuf/client"
)

// ErrTufSrcNoHash is returned if the TUF entry doesn't have a SHA512 hash
var ErrTufSrcNoHash = errors.New("tufsource: hash missing or wrong type")

// TUFSource wraps a TUF Client into the Source interface
type TUFSource struct {
	Client   *client.Client
	Interval time.Duration
}

// AvailableUpdates takes a list of Packages and returns a map from those Packages
// to any available update Package
func (f *TUFSource) AvailableUpdates(pkgs []*pkg.Package) (map[pkg.Package]pkg.Package, error) {
	_, err := f.Client.Update()
	if err != nil && !client.IsLatestSnapshot(err) {
		return nil, err
	}

	// TODO(jmatt) seems like 'm' should be the same as returned from
	// Client.Update, but empirically this seems untrue, investigate
	m, err := f.Client.Targets()

	if err != nil {
		return nil, err
	}

	updates := make(map[pkg.Package]pkg.Package)

	for _, p := range pkgs {
		meta, ok := m[p.Name]
		if !ok {
			continue
		}
		hash, ok := meta.Hashes["sha512"]
		if !ok {
			continue
		}

		hashStr := hash.String()
		if hashStr != p.Version {
			lg.Log.Printf("tufsource: available update %s version %s\n",
				p.Name, hashStr[:8])
			updates[*p] = pkg.Package{Name: p.Name, Version: hashStr}
		}
	}

	return updates, nil
}

// create a wrapper for File so it conforms to interface Client.Download expects
type delFile struct {
	*os.File
}

// Delete removes the file from the filesystem
func (f *delFile) Delete() error {
	f.Close()
	return os.Remove(f.Name())
}

// FetchPkg gets the content for the requested Package
func (f *TUFSource) FetchPkg(pkg *pkg.Package) (*os.File, error) {
	lg.Log.Printf("tufsource: requesting download for: %s\n", pkg.Name)
	tmp, err := ioutil.TempFile("", pkg.Version)
	if err != nil {
		return nil, err
	}

	err = f.Client.Download(pkg.Name, &delFile{tmp})
	if err != nil {
		return nil, ErrNoUpdateContent
	}

	_, err = tmp.Seek(0, os.SEEK_SET)
	if err != nil {
		tmp.Close()
		os.Remove(tmp.Name())
		return nil, err
	}
	return tmp, nil
}

// CheckInterval returns the time between which checks should be spaced.
func (f *TUFSource) CheckInterval() time.Duration {
	// TODO(jmatt) figure out how to establish a real value from the
	// Client we wrap
	return f.Interval
}

// Equals returns true if the Source passed in is a pointer to this instance
func (f *TUFSource) Equals(o Source) bool {
	switch o.(type) {
	case *TUFSource:
		return f == o.(*TUFSource)
	default:
		return false
	}
}
