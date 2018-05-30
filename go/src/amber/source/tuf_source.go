// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package source

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"time"

	"amber/lg"
	"amber/pkg"

	tuf "github.com/flynn/go-tuf/client"
	tuf_data "github.com/flynn/go-tuf/data"
	"github.com/flynn/go-tuf/verify"
)

// ErrTufSrcNoHash is returned if the TUF entry doesn't have a SHA512 hash
var ErrTufSrcNoHash = errors.New("tufsource: hash missing or wrong type")

type Config struct {
	URL       string
	LocalPath string
	Keys      []*tuf_data.Key
}

// TUFSource wraps a TUF Client into the Source interface
type TUFSource struct {
	Client   *tuf.Client
	Interval time.Duration
	Limit    uint64
	Config   *Config
}

type merkle struct {
	Root string `json:"merkle"`
}

type RemoteStoreError struct {
	error
}

type IOError struct {
	error
}

func LoadKeys(path string) ([]*tuf_data.Key, error) {
	f, err := os.Open(path)
	defer f.Close()
	if err != nil {
		return nil, err
	}

	var keys []*tuf_data.Key
	err = json.NewDecoder(f).Decode(&keys)
	return keys, err
}

func NewTUFSource(url string, path string, keys []*tuf_data.Key, interval time.Duration,
	limit uint64) *TUFSource {
	c := Config{
		URL:       url,
		LocalPath: path,
		Keys:      keys,
	}

	src := TUFSource{
		Interval: interval,
		Limit:    limit,
		Config:   &c,
	}

	return &src
}

func (f *TUFSource) initSrc() error {
	if f.Client != nil {
		return nil
	}

	client, store, err := newTUFClient(f.Config.URL, f.Config.LocalPath)

	if err != nil {
		return err
	}

	needs, err := needsInit(store)
	if err != nil {
		return fmt.Errorf("source status check failed: %s", err)
	}

	if needs {
		err := client.Init(f.Config.Keys, len(f.Config.Keys))
		if err != nil {
			return fmt.Errorf("TUF init failed: %s", err)
		}
	}

	f.Client = client
	return nil
}

// AvailableUpdates takes a list of Packages and returns a map from those Packages
// to any available update Package
func (f *TUFSource) AvailableUpdates(pkgs []*pkg.Package) (map[pkg.Package]pkg.Package, error) {
	if err := f.initSrc(); err != nil {
		return nil, fmt.Errorf("tuf_source: source could not be initialized: %s", err)
	}
	_, err := f.Client.Update()
	if err != nil && !tuf.IsLatestSnapshot(err) {
		if _, ok := err.(tuf.ErrDecodeFailed); ok {
			e := err.(tuf.ErrDecodeFailed)
			if _, ok := e.Err.(verify.ErrLowVersion); ok {
				err = fmt.Errorf("tuf_source: verify update repository is current or reset "+
					"device storage %s", err)
			}
		}
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

		m := merkle{}
		if meta.Custom != nil {
			json.Unmarshal(*meta.Custom, &m)
		}

		if (len(p.Version) == 0 || p.Version == hashStr) &&
			(len(p.Merkle) == 0 || p.Merkle == m.Root) {
			lg.Log.Printf("tuf_source: available update %s version %s\n",
				p.Name, hashStr[:8])
			updates[*p] = pkg.Package{
				Name:    p.Name,
				Version: hashStr,
				Merkle:  m.Root,
			}
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
	if err := f.initSrc(); err != nil {
		return nil, fmt.Errorf("tuf_source: source could not be initialized: %s", err)
	}
	lg.Log.Printf("tuf_source: requesting download for: %s\n", pkg.Name)
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

func (f *TUFSource) CheckLimit() uint64 {
	return f.Limit
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

func newTUFClient(url string, path string) (*tuf.Client, tuf.LocalStore, error) {
	tufStore, err := tuf.FileLocalStore(path)
	if err != nil {
		return nil, nil, IOError{fmt.Errorf("couldn't open datastore: %s", err)}
	}

	server, err := tuf.HTTPRemoteStore(url, nil, nil)
	if err != nil {
		return nil, nil, RemoteStoreError{fmt.Errorf("server address not understood: %s", err)}
	}

	return tuf.NewClient(tufStore, server), tufStore, nil
}

func needsInit(s tuf.LocalStore) (bool, error) {
	meta, err := s.GetMeta()
	if err != nil {
		return false, err
	}

	_, ok := meta["root.json"]
	return !ok, nil
}
