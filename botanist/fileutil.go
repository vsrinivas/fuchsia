// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"archive/tar"
	"fmt"
	"net"

	"fuchsia.googlesource.com/tools/tftp"
)

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
