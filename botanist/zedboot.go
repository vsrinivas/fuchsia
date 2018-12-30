// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package botanist

import (
	"archive/tar"
	"context"
	"fmt"
	"log"
	"net"
	"path"
	"time"

	"fuchsia.googlesource.com/tools/fastboot"
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

// FastbootToZedboot uses fastboot to flash and boot into Zedboot.
func FastbootToZedboot(ctx context.Context, fastbootTool, zirconRPath string) error {
	// If it can't find any fastboot device, the fastboot tool will hang
	// waiting, so we add a timeout.
	// All fastboot operations take less than a second on a developer
	// workstation, so a minute per each operation is very generous.
	ctx, _ = context.WithTimeout(ctx, 2*time.Minute)
	f := fastboot.Fastboot{fastbootTool}
	log.Printf("fastboot flashing zedboot image")
	if _, err := f.Flash(ctx, "boot", zirconRPath); err != nil {
		return fmt.Errorf("failed to flash the fastboot device: %v", err)
	}
	log.Printf("continuing from fastboot into zedboot")
	if _, err := f.Continue(ctx); err != nil {
		return fmt.Errorf("failed to boot the device with \"fastboot continue\": %v", err)
	}
	return nil
}
