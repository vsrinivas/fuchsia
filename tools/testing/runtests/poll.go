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
	"os"
	"path"
	"path/filepath"
	"sync"
	"time"

	"go.fuchsia.dev/fuchsia/tools/botanist/lib"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/lib/tarutil"
	"go.fuchsia.dev/fuchsia/tools/net/tftp"
)

// PollForSummary polls a node waiting for a summary.json to be written; this relies on
// runtests having been run on target.
func PollForSummary(ctx context.Context, t tftp.Client, summaryFilename, testResultsDir, outputArchive string, filePollInterval time.Duration) error {
	var buffer bytes.Buffer
	var writer io.WriterTo
	var err error
	err = retry.Retry(ctx, retry.NewConstantBackoff(filePollInterval), func() error {
		writer, err = t.Read(ctx, path.Join(testResultsDir, summaryFilename))
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

	if err = tarutil.TarBytes(tw, buffer.Bytes(), summaryFilename); err != nil {
		return err
	}

	logger.Debugf(ctx, "copying test output\n")

	// Tar in a subroutine while busy-printing so that we do not hit an i/o timeout when
	// dealing with large files.
	c := make(chan error)
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	numTasks := len(result.Outputs) + len(result.Tests)
	var tarLock sync.Mutex
	// Copy test output from the node.
	for _, output := range result.Outputs {
		go func(output string) {
			remote := filepath.Join(testResultsDir, output)
			err := botanist.FetchAndArchiveFile(ctx, t, tw, remote, output, &tarLock)
			c <- err
		}(output)
	}
	for _, test := range result.Tests {
		go func(test TestDetails) {
			remote := filepath.Join(testResultsDir, test.OutputFile)
			err := botanist.FetchAndArchiveFile(ctx, t, tw, remote, test.OutputFile, &tarLock)
			c <- err
		}(test)
		// Copy data sinks if any are present.
		for _, sinks := range test.DataSinks {
			numTasks += len(sinks)
			for _, sink := range sinks {
				go func(sink DataSink) {
					remote := filepath.Join(testResultsDir, sink.File)
					err := botanist.FetchAndArchiveFile(ctx, t, tw, remote, sink.File, &tarLock)
					c <- err
				}(sink)
			}
		}
	}

	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()
	go func() {
		for range ticker.C {
			logger.Debugf(ctx, "tarring test output...\n")
		}
	}()

	for i := 0; i < numTasks; i++ {
		if err := <-c; err != nil {
			return err
		}
	}

	return nil
}
