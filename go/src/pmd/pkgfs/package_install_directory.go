// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"bytes"
	"encoding/json"
	"io"
	"log"
	"os"
	"regexp"
	"strings"
	"time"

	"thinfs/fs"

	"fuchsia.googlesource.com/far"
	"fuchsia.googlesource.com/pm/pkg"
)

const (
	identityBlob = "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b"
)

var merklePat = regexp.MustCompile("^[0-9a-f]{64}$")

/*
The following VNodes are defined in this file:

/install           - installDir
/install/pkg       - installPkgDir
/install/pkg/{f}   - installFile{isPkg:true}
/install/blob      - installBlobDir
/install/blob/{f}  - installFile{isPkg:false}
*/

// installDir is located at /install.
type installDir struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *installDir) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *installDir) Stat() (int64, time.Time, time.Time, error) {
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

func (d *installDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	if name == "" {
		return nil, d, nil, nil
	}

	parts := strings.SplitN(name, "/", 2)

	var nd fs.Directory
	switch parts[0] {
	case "pkg":
		nd = &installPkgDir{fs: d.fs}
	case "blob":
		nd = &installBlobDir{fs: d.fs}
	default:
		return nil, nil, nil, fs.ErrNotFound
	}

	if len(parts) == 1 {
		if flags.Directory() {
			return nil, nd, nil, nil
		}
		return nil, nd, nil, fs.ErrNotSupported
	}

	return nd.Open(parts[1], flags)
}

func (d *installDir) Read() ([]fs.Dirent, error) {
	return []fs.Dirent{dirDirEnt("pkg"), dirDirEnt("blob")}, nil
}

func (d *installDir) Close() error {
	return nil
}

// installPkgDir is located at /install/pkg
type installPkgDir struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *installPkgDir) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *installPkgDir) Stat() (int64, time.Time, time.Time, error) {
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

func (d *installPkgDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	if name == "" {
		return nil, d, nil, nil
	}

	if !flags.Create() {
		return nil, nil, nil, fs.ErrNotSupported
	}

	f := &installFile{fs: d.fs, name: name, isPkg: true}
	err := f.open()
	return f, nil, nil, err
}

func (d *installPkgDir) Read() ([]fs.Dirent, error) {
	return []fs.Dirent{}, nil
}

func (d *installPkgDir) Close() error {
	return nil
}

// installBlobDir is located at /install/blob
type installBlobDir struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *installBlobDir) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *installBlobDir) Stat() (int64, time.Time, time.Time, error) {
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

func (d *installBlobDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	if name == "" {
		return nil, d, nil, nil
	}

	// TODO(raggi): support write resumption..

	if !flags.Create() {
		return nil, nil, nil, fs.ErrNotSupported
	}

	f := &installFile{fs: d.fs, name: name, isPkg: false}
	err := f.open()
	return f, nil, nil, err
}

func (d *installBlobDir) Read() ([]fs.Dirent, error) {
	return []fs.Dirent{}, nil
}

func (d *installBlobDir) Close() error {
	return nil
}

type installFile struct {
	unsupportedFile
	fs *Filesystem

	isPkg bool

	size    uint64
	written uint64
	name    string

	blob *os.File
}

func (f *installFile) open() error {
	if !merklePat.Match([]byte(f.name)) {
		return fs.ErrInvalidArgs
	}

	var err error
	// TODO(raggi): propagate flags instead to allow for resumption and so on
	f.blob, err = os.OpenFile(f.fs.blobfs.PathOf(f.name), os.O_WRONLY|os.O_CREATE, os.ModePerm)
	// permission errors from blobfs are returned when the blob already exists and
	// is no longer writable
	if os.IsPermission(err) {
		f.blob.Close()

		pkgs := f.fs.index.Fulfill(f.name)
		if f.isPkg {
			if err = importPackage(f.fs, f.name); err != nil {
				return goErrToFSErr(err)
			}
		} else {
			// in theory this never happens because this means the incoming blob
			// already existed in blobfs and therefore we shouldn't have a
			// a pending fulfillment which just happened, but this shouldn't be
			// harmful
			notifyActivation(f.fs, pkgs)
		}
		return fs.ErrAlreadyExists
	}
	if err != nil {
		f.blob.Close()
	}
	return goErrToFSErr(err)
}

func (f *installFile) Write(p []byte, off int64, whence int) (int, error) {
	if whence != fs.WhenceFromCurrent || off != 0 {
		return 0, fs.ErrNotSupported
	}

	n, err := f.blob.Write(p)
	f.written += uint64(n)

	return n, goErrToFSErr(err)
}

func (f *installFile) Close() error {
	if err := f.blob.Close(); err != nil {
		log.Printf("error closing file: %s\n", err)
		return goErrToFSErr(err)
	}

	// Wait until after the file is closed before importing the package in
	// case there is a merkle error.
	pkgs := f.fs.index.Fulfill(f.name)

	if f.isPkg {
		// TODO(raggi): use already open file instead of re-opening the file
		if err := importPackage(f.fs, f.name); err != nil {
			return goErrToFSErr(err)
		}
	} else {
		notifyActivation(f.fs, pkgs)
	}

	return nil
}

func (f *installFile) Stat() (int64, time.Time, time.Time, error) {
	return int64(f.written), time.Time{}, time.Time{}, nil
}

func (f *installFile) Truncate(sz uint64) error {
	f.size = sz
	err := f.blob.Truncate(int64(f.size))

	if f.size == 0 && f.name == identityBlob && err == nil {
		activations := f.fs.index.Fulfill(f.name)
		notifyActivation(f.fs, activations)
	}

	return goErrToFSErr(err)
}

// importPackage reads a package far from blobfs, given a content key, and imports it into the package index
func importPackage(filesystem *Filesystem, root string) error {
	log.Printf("pkgfs: importing package from %q", root)

	f, err := filesystem.blobfs.Open(root)
	if err != nil {
		log.Printf("error importing package: %s", err)
		return err
	}
	defer f.Close()

	// TODO(raggi): this is a bit messy, the system could instead force people to
	// write to specific paths in the incoming directory
	if !far.IsFAR(f) {
		log.Printf("pkgfs:importPackage: %q is not a package, ignoring import", root)
		return fs.ErrInvalidArgs
	}
	f.Seek(0, io.SeekStart)

	r, err := far.NewReader(f)
	if err != nil {
		log.Printf("error reading package archive package: %s", err)
		return err
	}

	// TODO(raggi): this can also be replaced if we enforce writes into specific places in the incoming tree
	var isPkg bool
	for _, f := range r.List() {
		if strings.HasPrefix(f, "meta/") {
			isPkg = true
		}
	}
	if !isPkg {
		log.Printf("pkgfs: %q does not contain a meta directory, assuming it is not a package", root)
		return fs.ErrInvalidArgs
	}

	pf, err := r.ReadFile("meta/package")
	if err != nil {
		log.Printf("error reading package metadata: %s", err)
		return err
	}

	var p pkg.Package
	err = json.Unmarshal(pf, &p)
	if err != nil {
		log.Printf("error parsing package metadata: %s", err)
		return fs.ErrInvalidArgs
	}

	if err := p.Validate(); err != nil {
		log.Printf("pkgfs: package is invalid: %s", err)
		return fs.ErrInvalidArgs
	}

	contents, err := r.ReadFile("meta/contents")
	if err != nil {
		log.Printf("pkgfs: error parsing package contents file for %s: %s", p, err)
		return fs.ErrInvalidArgs
	}

	files := bytes.Split(contents, []byte{'\n'})

	// Note: the following heuristic is coarse and not easy to compute. For small
	// packages enumerating all blobs in blobfs will be slow, but for very large
	// packages it's more likely that polling blobfs the expensive way for missing
	// blobs is going to be expensive. This can be improved by blobfs providing an
	// API to handle this case of "which of the following blobs are already
	// readable"
	mayHaveBlob := func(root string) bool { return true }
	if len(files) > 20 {
		d, err := os.Open(filesystem.blobfs.Root)
		if err != nil {
			log.Printf("pkgfs: error open(%q): %s", filesystem.blobfs.Root, err)
			return err
		}
		dnames, err := d.Readdirnames(-1)
		d.Close()
		if err != nil {
			log.Printf("pkgfs: error readdir(%q): %s", filesystem.blobfs.Root, err)
			return err
		}
		names := map[string]struct{}{}
		for _, name := range dnames {
			names[name] = struct{}{}
		}
		mayHaveBlob = func(root string) bool {
			_, found := names[root]
			return found
		}
	}

	needBlobs := map[string]struct{}{}
	for i := range files {
		parts := bytes.SplitN(files[i], []byte{'='}, 2)
		if len(parts) != 2 {
			// TODO(raggi): log illegal contents format?
			continue
		}
		root := string(parts[1])

		// XXX(raggi): this can race, which can deadlock package installs
		if mayHaveBlob(root) && filesystem.blobfs.HasBlob(root) {
			log.Printf("pkgfs: blob already present for %s: %q", p, root)
			continue
		}

		needBlobs[root] = struct{}{}
	}

	if len(needBlobs) == 0 {
		filesystem.index.Add(p, root)
		notifyActivation(filesystem, []string{root})
	} else {
		filesystem.index.AddNeeds(root, p, needBlobs)
	}

	// in order to background the amber calls, we have to make a new copy of the
	// blob list, as the index has taken write ownership over the map via AddNeeds
	var needList = make([]string, 0, len(needBlobs))
	for blob := range needBlobs {
		needList = append(needList, blob)
	}
	go func() {
		log.Printf("pkgfs: asking amber to fetch %d blobs for %s", len(needList), p)
		for _, root := range needList {
			filesystem.amberPxy.GetBlob(root)
		}
	}()

	return nil
}

func notifyActivation(fs *Filesystem, roots []string) {
	if len(roots) == 0 {
		return
	}

	go func() {
		fs.amberPxy.PackagesActivated(roots)
	}()
}
