// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"archive/tar"
	"bytes"
	"context"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"sync"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/net/tftp"
)

type retryTftpClient struct {
	failCount int
	failLimit int
	*tftp.ClientImpl
}

func (t *retryTftpClient) Read(ctx context.Context, path string) (*bytes.Reader, error) {
	if t.failCount < t.failLimit {
		t.failCount++
		return nil, fmt.Errorf("failed :(")
	}
	return bytes.NewReader([]byte{42, 42, 42}), nil
}

// TODO(fxb/43500): Remove once we are no longer using archives.
func TestFetchAndArchiveFile(t *testing.T) {
	ta, err := ioutil.TempFile("", t.Name())
	if err != nil {
		t.Fatalf("failed to create temp file: %s", err)
	}
	defer os.Remove(ta.Name())
	defer ta.Close()

	tw := tar.NewWriter(ta)
	defer tw.Close()

	client, err := tftp.NewClient(nil)
	if err != nil {
		t.Fatalf("failed to create tftp client: %s", err)
	}

	tftp := &retryTftpClient{
		failLimit:  1,
		ClientImpl: client,
	}

	if err := FetchAndCopyFile(context.Background(), tftp, tw, "test/test", "test", &sync.Mutex{}, ""); err != nil {
		t.Errorf("FetchAndArchive failed: %s", err)
	}
}

func TestFetchAndCopyFile(t *testing.T) {
	client, err := tftp.NewClient(nil)
	if err != nil {
		t.Fatalf("failed to create tftp client: %s", err)
	}

	tftp := &retryTftpClient{
		failLimit:  1,
		ClientImpl: client,
	}

	outDir, err := ioutil.TempDir("", "out")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(outDir)
	if err := FetchAndCopyFile(context.Background(), tftp, nil, "test/test", "test", &sync.Mutex{}, outDir); err != nil {
		t.Errorf("FetchAndCopy failed: %s", err)
	}
	// Try to read from copied file.
	expectedFile := filepath.Join(outDir, "test")
	if _, err := ioutil.ReadFile(expectedFile); err != nil {
		t.Errorf("failed to read from copied file %s: %v", expectedFile, err)
	}
}
