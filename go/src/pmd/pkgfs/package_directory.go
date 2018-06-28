// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"bytes"
	"encoding/json"
	"io/ioutil"
	"log"
	"path/filepath"
	"strings"
	"thinfs/fs"
	"time"

	"fuchsia.googlesource.com/far"
	"fuchsia.googlesource.com/pm/pkg"
)

type packageDir struct {
	unsupportedDirectory
	fs            *Filesystem
	name, version string
	contents      map[string]string

	// if this packagedir is a subdirectory, then this is the prefix name
	subdir *string
}

func newPackageDir(name, version string, filesystem *Filesystem) (*packageDir, error) {
	var merkleroot string
	var foundInStatic bool
	if filesystem.static != nil {
		merkleroot, foundInStatic = filesystem.static.Get(pkg.Package{Name: name, Version: version})
	}

	if !foundInStatic {
		bmerkle, err := ioutil.ReadFile(filesystem.index.PackageVersionPath(name, version))
		if err != nil {
			return nil, goErrToFSErr(err)
		}
		merkleroot = string(bmerkle)
	}

	return newPackageDirFromBlob(merkleroot, filesystem)
}

// Initialize a package directory server interface from a package meta.far
func newPackageDirFromBlob(blob string, filesystem *Filesystem) (*packageDir, error) {
	f, err := filesystem.blobfs.Open(blob)
	if err != nil {
		log.Printf("pkgfs: failed to open package contents at %q: %s", blob, err)
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
		unsupportedDirectory: unsupportedDirectory(filepath.Join("/packages", p.Name, p.Version)),
		name:                 p.Name,
		version:              p.Version,
		fs:                   filesystem,
		contents:             map[string]string{},
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
		pd.contents[string(parts[0])] = string(parts[1])
	}
	if err != nil {
		return nil, goErrToFSErr(err)
	}

	pd.contents["meta"] = blob
	for _, name := range fr.List() {
		if !strings.HasPrefix(name, "meta/") {
			log.Printf("pkgfs:packageDir:new %q/%q illegal file in meta.far: %q", pd.name, pd.version, name)
			continue
		}
		pd.contents[name] = name
	}

	return &pd, nil
}

func (d *packageDir) Close() error {
	debugLog("pkgfs:packageDir:close %q/%q", d.name, d.version)
	return nil
}

func (d *packageDir) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *packageDir) Reopen(flags fs.OpenFlags) (fs.Directory, error) {
	return d, nil
}

func (d *packageDir) getBlobFor(path string) (string, bool) {
	root, ok := d.contents[path]
	return root, ok
}

func (d *packageDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	debugLog("pkgfs:packagedir:open %q", name)

	if d.subdir != nil {
		name = filepath.Join(*d.subdir, name)
	}

	if name == "" {
		return nil, d, nil, nil
	}

	if flags.Create() || flags.Truncate() || flags.Write() || flags.Append() {
		debugLog("pkgfs:packagedir:open %q unsupported flags", name)
		return nil, nil, nil, fs.ErrNotSupported
	}

	if name == "meta" {
		mfd := newMetaFarDir(d.name, d.version, d.contents[name], d.fs)
		return nil, mfd, nil, nil
	}

	if strings.HasPrefix(name, "meta/") {
		if _, found := d.contents[name]; !found {
			return nil, nil, nil, fs.ErrNotFound
		}

		mfd := newMetaFarDir(d.name, d.version, d.contents["meta"], d.fs)
		return mfd.Open(strings.TrimPrefix(name, "meta"), flags)
	}

	if root, ok := d.contents[name]; ok {
		return nil, nil, &fs.Remote{Channel: d.fs.blobfs.Channel(), Path: root, Flags: flags}, nil
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

	debugLog("pkgfs:packagedir:open %q not found", name)
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
			// TODO(PKG-44): fix the potential for discrepancies here
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
	debugLog("pkgfs:packagedir:stat %q/%q", d.name, d.version)
	// TODO(raggi): forward stat values from the index
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}
