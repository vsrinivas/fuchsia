// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package botanist

import (
	"archive/tar"
	"fmt"
	"net"
	"path"

	"fuchsia.googlesource.com/tools/tftp"
)

func TransferAndWriteFileToTar(client *tftp.Client, tftpAddr *net.UDPAddr, tw *tar.Writer, testResultsDir string, outputFile string) error {
	writer, err := client.Receive(tftpAddr, path.Join(testResultsDir, outputFile))
	if err != nil {
		return fmt.Errorf("failed to receive file %s: %v\n", outputFile, err)
	}
	hdr := &tar.Header{
		Name: outputFile,
		Size: writer.(tftp.Session).Size(),
		Mode: 0666,
	}
	if err := tw.WriteHeader(hdr); err != nil {
		return fmt.Errorf("failed to write file header: %v\n", err)
	}
	if _, err := writer.WriteTo(tw); err != nil {
		return fmt.Errorf("failed to write file content: %v\n", err)
	}

	return nil
}
