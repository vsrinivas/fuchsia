// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"archive/tar"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/tools/tftp"
)

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
