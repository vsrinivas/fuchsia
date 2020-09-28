// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"bytes"
	"encoding/json"
	"log"
	"os"
	"path/filepath"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/garnet/go/src/far"
	"go.fuchsia.dev/fuchsia/garnet/go/src/thinfs/fs"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/pkg"
)

type packagedItem struct {
	blobId     string
	executable bool
}

type packageDir struct {
	unsupportedDirectory
	fs         *Filesystem
	merkleroot string
	contents   map[string]packagedItem
	executable bool

	// if this packagedir is a subdirectory, then this is the prefix name
	subdir *string
}

func newPackageDir(name, version string, filesystem *Filesystem, executable bool) (*packageDir, error) {
	var merkleroot string
	var foundInStatic bool
	p := pkg.Package{Name: name, Version: version}
	if filesystem.static != nil {
		merkleroot, foundInStatic = filesystem.static.Get(p)
	}

	if !foundInStatic {
		var found bool
		merkleroot, found = filesystem.index.Get(p)
		if !found {
			return nil, fs.ErrNotFound
		}
	}

	return newPackageDirFromBlob(merkleroot, filesystem, executable)
}

func isExecutablePath(path string) bool {
	// TODO(fxbug.dev/37328): try limiting this to just lib/, bin/, and test/
	// prefixes?  Or put explicit bits for each file in the manifest.
	return true
}

// Initialize a package directory server interface from a package meta.far
func newPackageDirFromBlob(blob string, filesystem *Filesystem, executable bool) (*packageDir, error) {
	f, err := filesystem.blobfs.Open(blob)
	if err != nil {
		if !os.IsNotExist(err) {
			log.Printf("pkgfs: failed to open package contents at %q: %s", blob, err)
		}
		return nil, goErrToFSErr(err)
	}
	defer f.Close()

	fr, err := far.NewReader(f)
	if err != nil {
		log.Printf("pkgfs: failed to read meta.far at %q: %s", blob, err)
		return nil, goErrToFSErr(err)
	}

	buf, err := fr.ReadFile("meta/package")
	if err != nil {
		log.Printf("pkgfs: failed to read meta/package from %q: %s", blob, err)
		return nil, goErrToFSErr(err)
	}
	var p pkg.Package
	if err := json.Unmarshal(buf, &p); err != nil {
		log.Printf("pkgfs: failed to parse meta/package from %q: %s", blob, err)
		return nil, goErrToFSErr(err)
	}

	buf, err = fr.ReadFile("meta/contents")
	if err != nil {
		log.Printf("pkgfs: failed to read meta/contents from %q: %s", blob, err)
		return nil, goErrToFSErr(err)
	}

	pd := packageDir{
		unsupportedDirectory: unsupportedDirectory("package:" + blob),
		merkleroot:           blob,
		fs:                   filesystem,
		contents:             map[string]packagedItem{},
		executable:           executable,
	}

	lines := bytes.Split(buf, []byte("\n"))

	for _, line := range lines {
		line = bytes.TrimSpace(line)
		if len(line) == 0 {
			continue
		}
		parts := bytes.SplitN(line, []byte("="), 2)
		if len(parts) != 2 {
			log.Printf("pkgfs: bad contents line: %v", line)
			continue
		}
		path := string(parts[0])
		pd.contents[path] = packagedItem{
			blobId:     string(parts[1]),
			executable: isExecutablePath(path),
		}
	}
	if err != nil {
		return nil, goErrToFSErr(err)
	}

	pd.contents["meta"] = packagedItem{
		blobId:     blob,
		executable: false,
	}
	for _, name := range fr.List() {
		if !strings.HasPrefix(name, "meta/") {
			log.Printf("package:%s illegal file in meta.far: %q", pd.merkleroot, name)
			continue
		}
		pd.contents[name] = packagedItem{
			blobId:     name,
			executable: false,
		}
	}

	return &pd, nil
}

func (d *packageDir) Close() error {
	return nil
}

func (d *packageDir) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *packageDir) getBlobFor(path string) (string, bool) {
	root, ok := d.contents[path]
	return root.blobId, ok
}

func (d *packageDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)

	if d.subdir != nil {
		name = filepath.Join(*d.subdir, name)
	}

	if name == "" {
		return nil, d, nil, nil
	}

	if flags.Create() || flags.Truncate() || flags.Write() || flags.Append() {
		return nil, nil, nil, fs.ErrNotSupported
	}

	if name == "meta" {
		if flags.File() || (!flags.Directory() && !flags.Path()) {
			mff := newMetaFile(d.contents[name].blobId, d.fs, flags)
			return mff, nil, nil, nil
		}
		mfd := newMetaFarDir(d.contents[name].blobId, d.fs)
		return nil, mfd, nil, nil
	}

	if strings.HasPrefix(name, "meta/") {
		mfd := newMetaFarDir(d.contents["meta"].blobId, d.fs)
		return mfd.Open(strings.TrimPrefix(name, "meta"), flags)
	}

	if root, ok := d.contents[name]; ok {
		if flags.Execute() {
			if !root.executable {
				return nil, nil, nil, fs.ErrPermission
			}

			// TODO(fxbug.dev/48930) Remove this temporary feature when possible.
			if !d.executable && d.fs.enforceNonBaseExecutabilityRestrictions {
				log.Printf("pkgfs: attempted executable open of %s. This is not allowed due to executability restrictions in pkgfs. See fxbug.dev/48902", name)
				return nil, nil, nil, fs.ErrPermission
			}
		}
		return nil, nil, &fs.Remote{Channel: d.fs.blobfs.Channel(), Path: root.blobId, Flags: flags}, nil

	}

	dirname := name + "/"
	for k := range d.contents {
		if strings.HasPrefix(k, dirname) {
			// subdir is a copy of d, but with subdir set
			subdir := *d
			subdir.subdir = &dirname
			return nil, &subdir, nil, nil
		}
	}

	return nil, nil, nil, fs.ErrNotFound
}

func (d *packageDir) Read() ([]fs.Dirent, error) {
	// TODO(raggi): improve efficiency
	dirs := map[string]struct{}{}
	dents := []fs.Dirent{}
	dents = append(dents, dirDirEnt("."))

	if d.subdir == nil {
		dirs["meta"] = struct{}{}
		dents = append(dents, dirDirEnt("meta"))
	}

	for name := range d.contents {
		if d.subdir != nil {
			if !strings.HasPrefix(name, *d.subdir) {
				continue
			}
			name = strings.TrimPrefix(name, *d.subdir)
		}

		parts := strings.SplitN(name, "/", 2)
		if len(parts) == 2 {
			if _, ok := dirs[parts[0]]; !ok {
				dirs[parts[0]] = struct{}{}
				dents = append(dents, dirDirEnt(parts[0]))
			}

		} else {
			// TODO(fxbug.dev/22014): fix the potential for discrepancies here
			// most of the time there are no pointers in contents for dirs, but the
			// exception is the meta pointer which this would mistake for a file, so we
			// must check for a name collision here too.
			if _, ok := dirs[parts[0]]; !ok {
				dents = append(dents, fileDirEnt(parts[0]))
			}
		}
	}
	return dents, nil
}

func (d *packageDir) Stat() (int64, time.Time, time.Time, error) {
	// TODO(raggi): forward stat values from the index
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

func (d *packageDir) Blobs() []string {
	// TODO(fxbug.dev/22235) consider preallocation which would over-allocate, but cause less thrash
	blobs := []string{}
	for path, blob := range d.contents {
		if strings.HasPrefix(path, "meta/") {
			continue
		}
		blobs = append(blobs, blob.blobId)
	}
	return blobs
}
