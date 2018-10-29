// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"path/filepath"
	"strings"
	"thinfs/fs"
	"time"
)

// versionsDirectory lists packages by merkleroot and enables opening packages
// by merkleroot.
type versionsDirectory struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *versionsDirectory) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *versionsDirectory) Close() error { return nil }

func (d *versionsDirectory) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	debugLog("pkgfs:versionsDirectory:open %q", name)
	if name == "" {
		return nil, d, nil, nil
	}

	parts := strings.Split(name, "/")

	if !merklePat.MatchString(parts[0]) {
		return nil, nil, nil, fs.ErrNotFound
	}

	pd, err := newPackageDirFromBlob(parts[0], d.fs)
	if err != nil {
		return nil, nil, nil, err
	}

	if len(parts) > 1 {
		return pd.Open(filepath.Join(parts[1:]...), flags)
	}

	if !(flags.Directory() || flags.Path()) || flags.Create() {
		return nil, nil, nil, fs.ErrNotSupported
	}

	return nil, pd, nil, nil
}

func (d *versionsDirectory) Read() ([]fs.Dirent, error) {
	versions := make(map[string]struct{})
	for _, merkle := range d.fs.static.PackageBlobs() {
		versions[merkle] = struct{}{}
	}
	// error ignored, it is useless here
	blobs, _ := d.fs.index.PackageBlobs()
	for _, merkle := range blobs {
		versions[merkle] = struct{}{}
	}

	dents := make([]fs.Dirent, 0, len(versions))
	for m := range versions {
		dents = append(dents, fileDirEnt(m))
	}
	return dents, nil
}

func (d *versionsDirectory) Stat() (int64, time.Time, time.Time, error) {
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}
