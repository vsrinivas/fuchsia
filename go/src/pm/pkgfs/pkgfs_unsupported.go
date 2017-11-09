// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"log"
	"time"

	"thinfs/fs"
)

var debug = false

func debugLog(s string, args ...interface{}) {
	if debug {
		log.Printf(s, args...)
	}
}

type unsupportedFile string

func (f unsupportedFile) Close() error {
	log.Printf("pkgfs:unsupported(%s): file close", string(f))
	return fs.ErrNotSupported
}

func (f unsupportedFile) Dup() (fs.File, error) {
	log.Printf("pkgfs:unsupported(%s): file dup", string(f))
	return f, fs.ErrNotSupported
}

func (f unsupportedFile) Read(p []byte, off int64, whence int) (int, error) {
	log.Printf("pkgfs:unsupported(%s): file read", string(f))
	return 0, fs.ErrNotSupported
}

func (f unsupportedFile) Reopen(flags fs.OpenFlags) (fs.File, error) {
	log.Printf("pkgfs:unsupported(%s): file reopen", string(f))
	return f, fs.ErrNotSupported
}

func (f unsupportedFile) Seek(offset int64, whence int) (int64, error) {
	log.Printf("pkgfs:unsupported(%s): file seek", string(f))
	return 0, fs.ErrNotSupported
}

func (f unsupportedFile) Stat() (int64, time.Time, time.Time, error) {
	log.Printf("pkgfs:unsupported(%s): file stat", string(f))
	return 0, time.Now(), time.Now(), fs.ErrNotSupported
}

func (f unsupportedFile) Sync() error {
	log.Printf("pkgfs:unsupported(%s): file seek", string(f))
	return fs.ErrNotSupported
}

func (f unsupportedFile) Tell() (int64, error) {
	log.Printf("pkgfs:unsupported(%s): file tell", string(f))
	return 0, fs.ErrNotSupported
}

func (f unsupportedFile) Touch(lastAccess, lastModified time.Time) error {
	log.Printf("pkgfs:unsupported(%s): file touch", string(f))
	return fs.ErrNotSupported
}

func (f unsupportedFile) Truncate(size uint64) error {
	log.Printf("pkgfs:unsupported(%s): file truncate", string(f))
	return fs.ErrNotSupported
}

func (f unsupportedFile) Write(p []byte, off int64, whence int) (int, error) {
	log.Printf("pkgfs:unsupported(%s): file write", string(f))
	return 0, fs.ErrNotSupported
}

func (f unsupportedFile) GetOpenFlags() fs.OpenFlags {
	log.Printf("pkgfs:unsupported(%s): get open flags", string(f))
	return 0
}

func (f unsupportedFile) SetOpenFlags(flags fs.OpenFlags) error {
	log.Printf("pkgfs:unsupported(%s): set open flags", string(f))
	return fs.ErrNotSupported
}

var _ = fs.File(unsupportedFile("impl-check"))

type unsupportedDirectory string

func (d unsupportedDirectory) Close() error {
	log.Printf("pkgfs:unsupported(%s): dir close", string(d))
	return fs.ErrNotSupported
}

func (d unsupportedDirectory) Dup() (fs.Directory, error) {
	log.Printf("pkgfs:unsupported(%s): dir dup", string(d))
	return nil, fs.ErrNotSupported
}

func (d unsupportedDirectory) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, error) {
	log.Printf("pkgfs:unsupported(%s): dir open: %s", string(d), name)
	return nil, nil, fs.ErrNotSupported
}

func (d unsupportedDirectory) Read() ([]fs.Dirent, error) {
	log.Printf("pkgfs:unsupported(%s): dir read", string(d))
	return nil, fs.ErrNotSupported
}

func (d unsupportedDirectory) Reopen(flags fs.OpenFlags) (fs.Directory, error) {
	log.Printf("pkgfs:unsupported(%s): dir reopen", string(d))
	return nil, fs.ErrNotSupported
}

func (d unsupportedDirectory) Rename(dstparent fs.Directory, src, dst string) error {
	log.Printf("pkgfs:unsupported(%s): dir rename", string(d))
	return fs.ErrNotSupported
}

func (d unsupportedDirectory) Stat() (int64, time.Time, time.Time, error) {
	log.Printf("pkgfs:unsupported(%s): dir stat", string(d))
	return 0, time.Now(), time.Now(), fs.ErrNotSupported
}

func (d unsupportedDirectory) Sync() error {
	log.Printf("pkgfs:unsupported(%s): dir sync", string(d))
	return fs.ErrNotSupported
}

func (d unsupportedDirectory) Touch(lastAccess, lastModified time.Time) error {
	log.Printf("pkgfs:unsupported(%s): dir touch", string(d))
	return fs.ErrNotSupported
}
func (d unsupportedDirectory) Unlink(target string) error {
	log.Printf("pkgfs:unsupported(%s): dir unlink", string(d))
	return fs.ErrNotSupported
}

var _ = fs.Directory(unsupportedDirectory("impl-check"))
