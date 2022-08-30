// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"io"
	iofs "io/fs"
	"os"
	"path/filepath"

	"github.com/kr/fs"
	"github.com/pkg/sftp"
)

// fsInterface contains the needed functions for interacting with the target
// file system (whether over SFTP or local emulation), i.e. the relevant
// intersection of `sftp.Client` and local filesystem methods.
//
// This allows for re-using the bulk of the code implementing Put/Get for both
// v1 and v2 fuzzers.
// TODO(fxbug.dev/106110): Simplify this once we don't need to support SFTP.
type fsInterface interface {
	iofs.FS
	iofs.GlobFS
	iofs.StatFS
	Walk(root string) *fs.Walker
	Create(name string) (writableFile, error)
	MkdirAll(path string) error
}

type writableFile interface {
	iofs.File
	io.WriteCloser
}

type localFs struct{}

func (f localFs) Glob(path string) ([]string, error) {
	return filepath.Glob(path)
}
func (f localFs) Open(path string) (iofs.File, error) {
	return os.Open(path)
}
func (f localFs) Walk(path string) *fs.Walker {
	return fs.Walk(path)
}
func (f localFs) Create(name string) (writableFile, error) {
	return os.Create(name)
}
func (f localFs) MkdirAll(path string) error {
	return os.MkdirAll(path, os.ModeDir|0o700)
}
func (f localFs) Stat(name string) (iofs.FileInfo, error) {
	return os.Stat(name)
}

type sftpFs struct {
	client *sftp.Client
}

func (f sftpFs) Glob(path string) ([]string, error) {
	return f.client.Glob(path)
}
func (f sftpFs) Open(path string) (iofs.File, error) {
	return f.client.Open(path)
}
func (f sftpFs) Walk(path string) *fs.Walker {
	return f.client.Walk(path)
}
func (f sftpFs) Create(name string) (writableFile, error) {
	return f.client.Create(name)
}
func (f sftpFs) MkdirAll(path string) error {
	return f.client.MkdirAll(path)
}
func (f sftpFs) Stat(name string) (iofs.FileInfo, error) {
	return f.client.Stat(name)
}
