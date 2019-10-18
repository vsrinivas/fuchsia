// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"strings"
	"thinfs/fs"
	"time"
)

// needsRoot presents the following tree:
//  /pkgfs/needs/packages/$PACKAGE_HASH/$BLOB_HASH
// the files are "needsFile" vnodes, so they're writable to blobfs.
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
	case "packages":
		npr := &needsPkgRoot{unsupportedDirectory: unsupportedDirectory("/needs/packages"), fs: d.fs}
		if len(parts) > 1 {
			return npr.Open(parts[1], flags)
		}
		return nil, npr, nil, nil

	default:
		if len(parts) != 1 || flags.Create() {
			return nil, nil, nil, fs.ErrNotSupported
		}

		return nil, nil, nil, fs.ErrNotFound
	}
}

func (d *needsRoot) Read() ([]fs.Dirent, error) {
	return []fs.Dirent{dirDirEnt("packages")}, nil
}

func (d *needsRoot) Stat() (int64, time.Time, time.Time, error) {
	// TODO(raggi): provide more useful values
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

// needsPkgRoot serves a directory that indexes the blobs needed to fulfill a
// package that is presently part way through caching.
type needsPkgRoot struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *needsPkgRoot) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *needsPkgRoot) Close() error {
	return nil
}

func (d *needsPkgRoot) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	if name == "" {
		return nil, d, nil, nil
	}

	parts := strings.SplitN(name, "/", 2)

	root := parts[0]

	if !d.fs.index.IsInstalling(root) {
		return nil, nil, nil, fs.ErrNotFound
	}

	pkgDir := &needsPkgDir{fs: d.fs, pkgRoot: root}

	if len(parts) > 1 {
		return pkgDir.Open(parts[1], flags)
	}

	return nil, pkgDir, nil, nil
}

func (d *needsPkgRoot) Read() ([]fs.Dirent, error) {
	blobs := d.fs.index.InstallingList()
	dirents := make([]fs.Dirent, len(blobs))
	for i := range blobs {
		dirents[i] = fileDirEnt(blobs[i])
	}
	return dirents, nil
}

func (d *needsPkgRoot) Stat() (int64, time.Time, time.Time, error) {
	// TODO(raggi): provide more useful values
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

// needsPkgDir serves a directory that indexes the blobs needed to fulfill a
// package that is presently part way through caching.
type needsPkgDir struct {
	unsupportedDirectory

	fs *Filesystem

	pkgRoot string
}

func (d *needsPkgDir) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *needsPkgDir) Close() error {
	return nil
}

func (d *needsPkgDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	if name == "" {
		return nil, d, nil, nil
	}

	if strings.Contains(name, "/") {
		return nil, nil, nil, fs.ErrNotFound
	}

	if !d.fs.index.PkgHasNeed(d.pkgRoot, name) {
		return nil, nil, nil, fs.ErrNotFound
	}

	return &installFile{fs: d.fs, name: name, isPkg: false}, nil, nil, nil
}

func (d *needsPkgDir) Read() ([]fs.Dirent, error) {
	names := d.fs.index.PkgNeedsList(d.pkgRoot)
	dirents := make([]fs.Dirent, len(names))
	for i := range names {
		dirents[i] = fileDirEnt(names[i])
	}
	return dirents, nil
}

func (d *needsPkgDir) Stat() (int64, time.Time, time.Time, error) {
	// TODO(raggi): provide more useful values
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}
