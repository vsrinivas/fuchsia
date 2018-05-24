// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"bytes"
	"encoding/json"
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

		// "Fulfill" any needs against the blob that was attempted to be written.
		f.fs.index.Fulfill(f.name)

		if f.isPkg {
			// A package is being written for which we already have an open blob in blobfs.
			// The error returned to the user is still ErrAlreadyExists, as the package
			// blob already exists, but we attempt to import it anyway.
			f.importPackage()
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

	// It is illegal to write past the truncated size of a blob.
	if f.written > f.size {
		return 0, fs.ErrInvalidArgs
	}

	n, err := f.blob.Write(p)
	f.written += uint64(n)

	if f.written >= f.size && err == nil {
		// "Fulfill" any needs against the blob that was attempted to be written.
		f.fs.index.Fulfill(f.name)

		if f.isPkg {
			// If a package installation fails, the error is reported here.
			return n, f.importPackage()
		}
	}

	// If a blob installation fails, the error is reported here.
	return n, goErrToFSErr(err)
}

func (f *installFile) Close() error {
	if err := f.blob.Close(); err != nil {
		log.Printf("error closing file: %s\n", err)
		return goErrToFSErr(err)
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
		// Fulfill any needs against the identity blob
		f.fs.index.Fulfill(f.name)
	}

	return goErrToFSErr(err)
}

// importPackage uses f.blob to import the package that was just written. It
// returns an fs.Error to report back to the user at write time.
func (f *installFile) importPackage() error {
	log.Printf("pkgfs: importing package from %q", f.name)

	b, err := f.fs.blobfs.Open(f.name)
	if err != nil {
		log.Printf("pkgfs: error opening package blob after writing: %s: %s", f.name, err)
		return fs.ErrFailedPrecondition
	}

	r, err := far.NewReader(b)
	if err != nil {
		log.Printf("pkgfs: error reading package archive: %s", err)
		// Note: translates to zx.ErrBadState
		return fs.ErrFailedPrecondition
	}

	pf, err := r.ReadFile("meta/package")
	if err != nil {
		log.Printf("pkgfs: error reading package metadata: %s", err)
		// Note: translates to zx.ErrBadState
		return fs.ErrFailedPrecondition
	}

	var p pkg.Package
	err = json.Unmarshal(pf, &p)
	if err != nil {
		log.Printf("pkgfs: error parsing package metadata: %s", err)
		// Note: translates to zx.ErrBadState
		return fs.ErrFailedPrecondition
	}

	if err := p.Validate(); err != nil {
		log.Printf("pkgfs: package is invalid: %s", err)
		// Note: translates to zx.ErrBadState
		return fs.ErrFailedPrecondition
	}

	contents, err := r.ReadFile("meta/contents")
	if err != nil {
		log.Printf("pkgfs: error parsing package contents file for %s: %s", p, err)
		// Note: translates to zx.ErrBadState
		return fs.ErrFailedPrecondition
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
		d, err := os.Open(f.fs.blobfs.Root)
		if err != nil {
			log.Printf("pkgfs: error open(%q): %s", f.fs.blobfs.Root, err)
			// Note: translates to zx.ErrBadState
			return fs.ErrFailedPrecondition
		}
		dnames, err := d.Readdirnames(-1)
		d.Close()
		if err != nil {
			log.Printf("pkgfs: error readdir(%q): %s", f.fs.blobfs.Root, err)
			// Note: translates to zx.ErrBadState
			return fs.ErrFailedPrecondition
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
			log.Printf("pkgfs: skipping bad package entry: %q", files[i])
			continue
		}
		root := string(parts[1])

		// XXX(raggi): this can race, which can deadlock package installs
		if mayHaveBlob(root) && f.fs.blobfs.HasBlob(root) {
			log.Printf("pkgfs: blob already present for %s: %q", p, root)
			continue
		}

		needBlobs[root] = struct{}{}
	}

	if len(needBlobs) == 0 {
		f.fs.index.Add(p, f.name)
	} else {
		f.fs.index.AddNeeds(f.name, p, needBlobs)
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
			f.fs.amberPxy.GetBlob(root)
		}
	}()

	return nil
}
