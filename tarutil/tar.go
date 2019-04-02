// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tarutil

import (
	"archive/tar"
	"io"
	"io/ioutil"
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

// TarBuffer writes the given bytes to a given path within an archive.
func TarBuffer(tw *tar.Writer, buf []byte, path string) error {
	hdr := &tar.Header{
		Name: path,
		Size: int64(len(buf)),
		Mode: 0666,
	}
	if err := tw.WriteHeader(hdr); err != nil {
		return err
	}
	_, err := tw.Write(buf)
	return err
}

// TarReader writes data from the given Reader to the given tar.Writer.
func TarReader(tw *tar.Writer, r io.Reader, path string) error {
	bytes, err := ioutil.ReadAll(r)
	if err != nil {
		return err
	}
	return TarBuffer(tw, bytes, path)
}
