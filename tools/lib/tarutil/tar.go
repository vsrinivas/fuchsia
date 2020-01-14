// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package tarutil provides methods for creating tar packages.
package tarutil

import (
	"archive/tar"
	"bytes"
	"io"
	"os"
	"path/filepath"
)

// TarDirectory archives the given directory.
func TarDirectory(tw *tar.Writer, dir string) error {
	return filepath.Walk(dir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if info.IsDir() {
			return nil
		}

		hdr, err := tar.FileInfoHeader(info, path)
		if err != nil {
			return err
		}
		hdr.Name = path[len(dir)+1:]
		if err := tw.WriteHeader(hdr); err != nil {
			return err
		}
		fi, err := os.Open(path)
		if err != nil {
			return err
		}
		_, err = io.Copy(tw, fi)
		return err
	})
}

// TarBytes writes the given bytes to a given path within an archive.
func TarBytes(tw *tar.Writer, b []byte, path string) error {
	return TarFromReader(tw, bytes.NewBuffer(b), path, int64(len(b)))
}

// TarFromReader writes data from the given Reader to the given tar.Writer.
func TarFromReader(tw *tar.Writer, r io.Reader, path string, size int64) error {
	hdr := &tar.Header{
		Name: path,
		Size: size,
		Mode: 0666,
	}
	if err := tw.WriteHeader(hdr); err != nil {
		return err
	}
	_, err := io.Copy(tw, r)
	return err
}

// TarFile writes the given file to an archive at the given path.
func TarFile(tw *tar.Writer, src, dest string) error {
	f, err := os.Open(src)
	if err != nil {
		return err
	}
	fi, err := f.Stat()
	if err != nil {
		return err
	}
	return TarFromReader(tw, f, dest, fi.Size())
}
