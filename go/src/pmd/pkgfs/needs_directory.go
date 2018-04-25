// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"fmt"
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

		if flags.Create() {
			// TODO(jmatt) PKG-57 support version properly rather than hard-coding
			go d.fs.amberPxy.GetUpdate(fmt.Sprintf("%s/0", parts[0]), nil, nil)
			return &needsFile{unsupportedFile: unsupportedFile(filepath.Join("/needs", name)), fs: d.fs}, nil, nil, nil
		}

		return nil, nil, nil, fs.ErrNotFound
	}
}

func (d *needsRoot) Read() ([]fs.Dirent, error) {
	return []fs.Dirent{dirDirEnt("blobs")}, nil
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

	if !d.fs.index.HasNeed(name) {
		return nil, nil, nil, fs.ErrNotFound
	}

	debugLog("pkgfs:needsblob:%q open", name)
	return &installFile{fs: d.fs, name: name, isPkg: false}, nil, nil, nil
}

func (d *needsBlobsDir) Read() ([]fs.Dirent, error) {
	names := d.fs.index.NeedsList()
	dirents := make([]fs.Dirent, len(names))
	for i := range names {
		dirents[i] = fileDirEnt(names[i])
	}
	return dirents, nil
}

func (d *needsBlobsDir) Stat() (int64, time.Time, time.Time, error) {
	// TODO(raggi): provide more useful values
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}
