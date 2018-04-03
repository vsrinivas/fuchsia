// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"thinfs/fs"
	"time"
)

type needsRoot struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *needsRoot) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *needsRoot) Close() error {
	return nil
}

func (d *needsRoot) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	if name == "" {
		return nil, d, nil, nil
	}

	parts := strings.SplitN(name, "/", 2)

	switch parts[0] {
	case "blobs":
		nbd := &needsBlobsDir{unsupportedDirectory: unsupportedDirectory("/needs/blobs"), fs: d.fs}
		if len(parts) > 1 {
			return nbd.Open(parts[1], flags)
		}
		return nil, nbd, nil, nil
	default:
		if len(parts) != 1 {
			return nil, nil, nil, fs.ErrNotSupported
		}

		idxPath := d.fs.index.NeedsFile(parts[0])

		var f *os.File
		var err error
		if flags.Create() {
			f, err = os.Create(idxPath)
			go d.fs.amberPxy.GetUpdate(parts[0], nil)
		} else {
			f, err = os.Open(idxPath)
		}
		if err != nil {
			return nil, nil, nil, goErrToFSErr(err)
		}
		if err := f.Close(); err != nil {
			return nil, nil, nil, goErrToFSErr(err)
		}
		return &needsFile{unsupportedFile: unsupportedFile(filepath.Join("/needs", name)), fs: d.fs}, nil, nil, nil
	}
}

func (d *needsRoot) Read() ([]fs.Dirent, error) {
	infos, err := ioutil.ReadDir(d.fs.index.NeedsDir())
	if err != nil {
		return nil, goErrToFSErr(err)
	}

	var dents = make([]fs.Dirent, len(infos))

	for i, info := range infos {
		if info.IsDir() {
			dents[i] = dirDirEnt(filepath.Base(info.Name()))
		} else {
			dents[i] = fileDirEnt(filepath.Base(info.Name()))
		}
	}

	return dents, nil
}

func (d *needsRoot) Stat() (int64, time.Time, time.Time, error) {
	// TODO(raggi): provide more useful values
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

type needsFile struct {
	unsupportedFile

	fs *Filesystem
}

func (f *needsFile) Close() error {
	return nil
}

func (f *needsFile) Stat() (int64, time.Time, time.Time, error) {
	return 0, time.Time{}, time.Time{}, nil
}

type needsBlobsDir struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *needsBlobsDir) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *needsBlobsDir) Close() error {
	return nil
}

func (d *needsBlobsDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	if name == "" {
		return nil, d, nil, nil
	}

	if strings.Contains(name, "/") {
		return nil, nil, nil, fs.ErrNotFound
	}

	if _, err := os.Stat(d.fs.index.NeedsBlob(name)); err != nil {
		return nil, nil, nil, goErrToFSErr(err)
	}

	debugLog("pkgfs:needsblob:%q open", name)
	return &installFile{fs: d.fs, name: name, isPkg: false}, nil, nil, nil
}

func (d *needsBlobsDir) Read() ([]fs.Dirent, error) {
	names, err := filepath.Glob(d.fs.index.NeedsBlob("*"))
	if err != nil {
		return nil, goErrToFSErr(err)
	}
	dirents := make([]fs.Dirent, len(names))
	for i := range names {
		dirents[i] = fileDirEnt(filepath.Base(names[i]))
	}
	return dirents, nil
}

func (d *needsBlobsDir) Stat() (int64, time.Time, time.Time, error) {
	// TODO(raggi): provide more useful values
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}
