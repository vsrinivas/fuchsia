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

	if name == "" {
		return nil, d, nil, nil
	}

	parts := strings.Split(name, "/")

	if !merklePat.MatchString(parts[0]) {
		return nil, nil, nil, fs.ErrNotFound
	}

	_, found := d.fs.index.GetRoot(parts[0])
	if !found {
		return nil, nil, nil, fs.ErrNotFound
	}

	pd, err := newPackageDirFromBlob(parts[0], d.fs)
	if err != nil {
		return nil, nil, nil, err
	}

	if len(parts) > 1 {
		return pd.Open(filepath.Join(parts[1:]...), flags)
	}

	if flags.Create() || flags.Truncate() || flags.Write() || flags.Append() || flags.File() {
		return nil, nil, nil, fs.ErrNotSupported
	}

	return nil, pd, nil, nil
}

func (d *versionsDirectory) Read() ([]fs.Dirent, error) {
	roots := d.fs.index.PackageBlobs()

	dents := make([]fs.Dirent, 0, len(roots))
	for _, m := range roots {
		dents = append(dents, fileDirEnt(m))
	}
	return dents, nil
}

func (d *versionsDirectory) Stat() (int64, time.Time, time.Time, error) {
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}
