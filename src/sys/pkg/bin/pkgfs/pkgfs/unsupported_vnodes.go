// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"log"
	"time"

	"thinfs/fs"
)

const (
	fileType = "file"
	dirType  = "dir"
)

func logUnsupportedOperation(node, nodeType, opName string) {
	log.Printf("unsupported(%s): %s %s", string(node), nodeType, opName)
}

type unsupportedFile string

// Export for testing.
type UnsupportedFile = unsupportedFile

func (f unsupportedFile) Close() error {
	logUnsupportedOperation(string(f), fileType, "close")
	return fs.ErrNotSupported
}

func (f unsupportedFile) Dup() (fs.File, error) {
	logUnsupportedOperation(string(f), fileType, "dup")
	return f, fs.ErrNotSupported
}

func (f unsupportedFile) Read(p []byte, off int64, whence int) (int, error) {
	logUnsupportedOperation(string(f), fileType, "read")
	return 0, fs.ErrNotSupported
}

func (f unsupportedFile) Reopen(flags fs.OpenFlags) (fs.File, error) {
	logUnsupportedOperation(string(f), fileType, "reopen")
	return f, fs.ErrNotSupported
}

func (f unsupportedFile) Seek(offset int64, whence int) (int64, error) {
	logUnsupportedOperation(string(f), fileType, "seek")
	return 0, fs.ErrNotSupported
}

func (f unsupportedFile) Stat() (int64, time.Time, time.Time, error) {
	logUnsupportedOperation(string(f), fileType, "stat")
	return 0, time.Now(), time.Now(), fs.ErrNotSupported
}

func (f unsupportedFile) Sync() error {
	logUnsupportedOperation(string(f), fileType, "sync")
	return fs.ErrNotSupported
}

func (f unsupportedFile) Tell() (int64, error) {
	logUnsupportedOperation(string(f), fileType, "tell")
	return 0, fs.ErrNotSupported
}

func (f unsupportedFile) Touch(lastAccess, lastModified time.Time) error {
	logUnsupportedOperation(string(f), fileType, "touch")
	return fs.ErrNotSupported
}

func (f unsupportedFile) Truncate(size uint64) error {
	logUnsupportedOperation(string(f), fileType, "truncate")
	return fs.ErrNotSupported
}

func (f unsupportedFile) Write(p []byte, off int64, whence int) (int, error) {
	logUnsupportedOperation(string(f), fileType, "write")
	return 0, fs.ErrNotSupported
}

func (f unsupportedFile) GetOpenFlags() fs.OpenFlags {
	logUnsupportedOperation(string(f), fileType, "get_open_flags")
	return 0
}

func (f unsupportedFile) SetOpenFlags(flags fs.OpenFlags) error {
	logUnsupportedOperation(string(f), fileType, "set_open_flags")
	return fs.ErrNotSupported
}

var _ = fs.File(unsupportedFile("impl-check"))

type unsupportedDirectory string

// Export for testing.
type UnsupportedDirectory = unsupportedDirectory

func (d unsupportedDirectory) Close() error {
	logUnsupportedOperation(string(d), dirType, "close")
	return fs.ErrNotSupported
}

func (d unsupportedDirectory) Dup() (fs.Directory, error) {
	logUnsupportedOperation(string(d), dirType, "dup")
	return nil, fs.ErrNotSupported
}

func (d unsupportedDirectory) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	logUnsupportedOperation(string(d), dirType, "open")
	return nil, nil, nil, fs.ErrNotSupported
}

func (d unsupportedDirectory) Read() ([]fs.Dirent, error) {
	logUnsupportedOperation(string(d), dirType, "read")
	return nil, fs.ErrNotSupported
}

func (d unsupportedDirectory) Reopen(flags fs.OpenFlags) (fs.Directory, error) {
	logUnsupportedOperation(string(d), dirType, "reopen")
	return nil, fs.ErrNotSupported
}

func (d unsupportedDirectory) Rename(dstparent fs.Directory, src, dst string) error {
	logUnsupportedOperation(string(d), dirType, "rename")
	return fs.ErrNotSupported
}

func (d unsupportedDirectory) Stat() (int64, time.Time, time.Time, error) {
	logUnsupportedOperation(string(d), dirType, "stat")
	return 0, time.Now(), time.Now(), fs.ErrNotSupported
}

func (d unsupportedDirectory) Sync() error {
	logUnsupportedOperation(string(d), dirType, "sync")
	return fs.ErrNotSupported
}

func (d unsupportedDirectory) Touch(lastAccess, lastModified time.Time) error {
	logUnsupportedOperation(string(d), dirType, "touch")
	return fs.ErrNotSupported
}
func (d unsupportedDirectory) Unlink(target string) error {
	logUnsupportedOperation(string(d), dirType, "unlink")
	return fs.ErrNotSupported
}

var _ = fs.Directory(unsupportedDirectory("impl-check"))
