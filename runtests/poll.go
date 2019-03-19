// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package runtests

import (
	"archive/tar"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"os"
	"path"
	"path/filepath"
	"time"

	"fuchsia.googlesource.com/tools/botanist"
	"fuchsia.googlesource.com/tools/logger"
	"fuchsia.googlesource.com/tools/retry"
	"fuchsia.googlesource.com/tools/tftp"
)

// PollForSummary polls a node waiting for a summary.json to be written; this relies on
// runtests having been run on target.
func PollForSummary(ctx context.Context, addr *net.UDPAddr, summaryFilename, testResultsDir, outputArchive string, filePollInterval time.Duration) error {
	t := tftp.NewClient()
	tftpAddr := &net.UDPAddr{
		IP:   addr.IP,
		Port: tftp.ClientPort,
		Zone: addr.Zone,
	}
	var buffer bytes.Buffer
	var writer io.WriterTo
	var err error
	err = retry.Retry(ctx, retry.NewConstantBackoff(filePollInterval), func() error {
		writer, err = t.Receive(tftpAddr, path.Join(testResultsDir, summaryFilename))
		return err
	}, nil)
	if err != nil {
		return fmt.Errorf("timed out waiting for tests to complete: %v", err)
	}

	logger.Debugf(ctx, "reading %q\n", summaryFilename)

	if _, err := writer.WriteTo(&buffer); err != nil {
		return fmt.Errorf("failed to receive summary file: %v", err)
	}

	// Parse and save the summary.json file.
	var result TestSummary
	if err := json.Unmarshal(buffer.Bytes(), &result); err != nil {
		return fmt.Errorf("cannot unmarshall test results: %v", err)
	}

	outFile, err := os.OpenFile(outputArchive, os.O_WRONLY|os.O_CREATE, 0666)
	if err != nil {
		return fmt.Errorf("failed to create file %s: %v", outputArchive, err)
	}

	tw := tar.NewWriter(outFile)
	defer tw.Close()

	if err = botanist.ArchiveBuffer(tw, buffer.Bytes(), summaryFilename); err != nil {
		return err
	}

	logger.Debugf(ctx, "copying test output\n")

	// Tar in a subroutine while busy-printing so that we do not hit an i/o timeout when
	// dealing with large files.
	c := make(chan error)
	go func() {
		// Copy test output from the node.
		for _, output := range result.Outputs {
			remote := filepath.Join(testResultsDir, output)
			if err = botanist.FetchAndArchiveFile(t, tftpAddr, tw, remote, output); err != nil {
				c <- err
				return
			}
		}
		for _, test := range result.Tests {
			remote := filepath.Join(testResultsDir, test.OutputFile)
			if err = botanist.FetchAndArchiveFile(t, tftpAddr, tw, remote, test.OutputFile); err != nil {
				c <- err
				return
			}
			// Copy data sinks if any are present.
			for _, sinks := range test.DataSinks {
				for _, sink := range sinks {
					remote := filepath.Join(testResultsDir, sink.File)
					if err = botanist.FetchAndArchiveFile(t, tftpAddr, tw, remote, sink.File); err != nil {
						c <- err
						return
					}
				}
			}
		}
		c <- nil
	}()

	logger.Debugf(ctx, "tarring test output...\n")
	ticker := time.NewTicker(5 * time.Second)
	for {
		select {
		case err := <-c:
			ticker.Stop()
			return err
		case <-ticker.C:
			logger.Debugf(ctx, "tarring test output...\n")
		}
	}
}
