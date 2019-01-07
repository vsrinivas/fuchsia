// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"archive/tar"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/tools/tftp"
)

// CopyFile copies a file at src to dest. An error is returned if there is already a file
// at dest.
func CopyFile(src, dest string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	info, err := in.Stat()
	if err != nil {
		return err
	}
	out, err := os.OpenFile(dest, os.O_WRONLY|os.O_CREATE, info.Mode().Perm())
	if err != nil {
		return err
	}
	defer out.Close()
	_, err = io.Copy(out, in)
	return err
}

// OverwriteFileWithCopy overwrites a given file with a copy of it.
//
// This is used to break any hard linking that might back the file. In
// particular, Swarming creates hard-links between Isolate downloads and a cache - and
// modifying those will modify the cache contents themselves; this is used to prevent
// that.
func OverwriteFileWithCopy(path string) error {
	copy, err := ioutil.TempFile("", "botanist")
	if err != nil {
		return err
	}
	if err = CopyFile(path, copy.Name()); err != nil {
		return err
	}
	if err = os.Remove(path); err != nil {
		return err
	}
	return os.Rename(copy.Name(), path)
}

// ArchiveDirectory archives the given directory.
func ArchiveDirectory(tw *tar.Writer, dir string) error {
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

// ArchiveBuffer writes the given bytes to a given path within an archive.
func ArchiveBuffer(tw *tar.Writer, buf []byte, path string) error {
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

// FetchAndArchiveFile fetches a remote file via TFTP from a given node, and
// writes it an archive.
func FetchAndArchiveFile(t *tftp.Client, addr *net.UDPAddr, tw *tar.Writer, path, name string) error {
	receiver, err := t.Receive(addr, path)
	if err != nil {
		return fmt.Errorf("failed to receive file %s: %v\n", path, err)
	}
	hdr := &tar.Header{
		Name: name,
		Size: receiver.(tftp.Session).Size(),
		Mode: 0666,
	}
	if err := tw.WriteHeader(hdr); err != nil {
		return err
	}
	_, err = receiver.WriteTo(tw)
	return err
}
