// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"bytes"
	"context"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
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
	if err := FetchAndCopyFile(context.Background(), tftp, "test/test", "test", outDir); err != nil {
		t.Errorf("FetchAndCopy failed: %s", err)
	}
	// Try to read from copied file.
	expectedFile := filepath.Join(outDir, "test")
	if _, err := ioutil.ReadFile(expectedFile); err != nil {
		t.Errorf("failed to read from copied file %s: %v", expectedFile, err)
	}
}
