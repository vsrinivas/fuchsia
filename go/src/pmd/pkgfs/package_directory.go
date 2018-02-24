// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"bufio"
	"bytes"
	"io"
	"io/ioutil"
	"log"
	"os"
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

	pd, err := newPackageDirFromBlob(merkleroot, filesystem)
	if err != nil {
		return nil, err
	}

	// update the name related fields for easier debugging:
	pd.unsupportedDirectory = unsupportedDirectory(filepath.Join("/packages", name, version))
	pd.name = name
	pd.version = version

	return pd, nil
}

// Initialize a package directory server interface from a package meta.far
func newPackageDirFromBlob(blob string, filesystem *Filesystem) (*packageDir, error) {
	blobPath := filepath.Join(filesystem.blobstore.Root, blob)
	f, err := os.Open(blobPath)
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

	buf, err := fr.ReadFile("meta/contents")
	if err != nil {
		log.Printf("pkgfs: failed to read meta/contents from %q: %s", blob, err)
		return nil, goErrToFSErr(err)
	}

	pd, err := newPackageDirFromReader(bytes.NewReader(buf), filesystem)
	if err != nil {
		return nil, goErrToFSErr(err)
	}
	pd.unsupportedDirectory = unsupportedDirectory(blob)
	pd.name = blob
	pd.version = ""

	pd.contents["meta"] = blob

	return pd, nil
}

func newPackageDirFromReader(r io.Reader, filesystem *Filesystem) (*packageDir, error) {
	pd := packageDir{
		unsupportedDirectory: unsupportedDirectory("packageDir"),
		fs:                   filesystem,
		contents:             map[string]string{},
	}

	b := bufio.NewReader(r)

	for {
		line, err := b.ReadString('\n')
		if err == io.EOF {
			if len(line) == 0 {
				break
			}
			err = nil
		}

		if err != nil {
			log.Printf("pkgfs: failed to read package contents from %v: %s", r, err)
			// TODO(raggi): better error?
			return nil, fs.ErrFailedPrecondition
		}
		line = strings.TrimSpace(line)
		parts := strings.SplitN(line, "=", 2)
		if len(parts) != 2 {
			log.Printf("pkgfs: bad contents line: %v", line)
			continue
		}
		pd.contents[parts[0]] = parts[1]
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

	if name == "meta" || strings.HasPrefix(name, "meta/") {
		if blob, ok := d.contents["meta"]; ok {
			d, err := newMetaFarDir(d.name, d.version, blob, d.fs)
			if err != nil {
				return nil, nil, nil, err
			}
			return d.Open(strings.TrimPrefix(name, "meta"), flags)
		}
		return nil, nil, nil, fs.ErrNotFound
	}

	if root, ok := d.contents[name]; ok {
		return nil, nil, &fs.Remote{Channel: d.fs.blobstore.Channel(), Path: root, Flags: flags}, nil
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
			dents = append(dents, fileDirEnt(parts[0]))
		}
	}
	return dents, nil
}

func (d *packageDir) Stat() (int64, time.Time, time.Time, error) {
	debugLog("pkgfs:packagedir:stat %q/%q", d.name, d.version)
	// TODO(raggi): forward stat values from the index
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}
