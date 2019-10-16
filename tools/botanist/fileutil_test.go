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

func TestFetchAndArchiveFile(t *testing.T) {
	ta, err := ioutil.TempFile("", t.Name())
	if err != nil {
		t.Fatalf("failed to create temp file: %s", err)
	}
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

	if err := FetchAndArchiveFile(context.Background(), tftp, tw, "test/test", "test", &sync.Mutex{}); err != nil {
		t.Errorf("FetchAndArchive failed: %s", err)
	}
}
