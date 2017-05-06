// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"time"

	"github.com/flynn/go-tuf/client"
)

var ErrTufSrcNoHash = errors.New("tufsource: hash missing or wrong type")

type ErrRetrieval string

func (s ErrRetrieval) Error() string {
	return fmt.Sprintf("updatesource: data retrieval failed %s", s)
}

type TUFSource struct {
	Client *client.Client
}

func (f *TUFSource) FetchUpdate(pkg *Package) (*Package, error) {
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

	meta, ok := m[pkg.Name]
	if !ok {
		return nil, ErrUnknownPkg
	}

	hash, ok := meta.Hashes["sha512"]
	if !ok {
		return nil, ErrTufSrcNoHash
	}

	hashStr := hash.String()
	if hashStr != pkg.Version {
		fmt.Printf("\nAvailable update %s version %s\n", pkg.Name,
			hashStr[:8])
		return &Package{Name: pkg.Name, Version: hashStr}, nil
	}

	return nil, ErrNoUpdate
}

// create a wrapper for File so it conforms to interface Client.Download expects
type delFile struct {
	*os.File
}

func (f *delFile) Delete() error {
	f.Close()
	return os.Remove(f.Name())
}

func (f *TUFSource) FetchPkg(pkg *Package) (*os.File, error) {
	fmt.Printf("Requesting download for: %s\n", pkg.Name)
	tmp, err := ioutil.TempFile("", pkg.Version)
	if err != nil {
		return nil, err
	}

	err = f.Client.Download(pkg.Name, &delFile{tmp})
	if err != nil {
		return nil, ErrRetrieval(err.Error())
	}

	_, err = tmp.Seek(0, os.SEEK_SET)
	if err != nil {
		tmp.Close()
		os.Remove(tmp.Name())
		return nil, err
	}
	return tmp, nil
}

func (f *TUFSource) CheckInterval() time.Duration {
	// TODO(jmatt) figure out how to establish a real value from the
	// Client we wrap
	return 1 * time.Second
}

func (f *TUFSource) Equals(o Source) bool {
	switch o.(type) {
	case *TUFSource:
		return f == o.(*TUFSource)
	default:
		return false
	}
}
