// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sshutil

import (
	"context"
	"testing"
)

func TestRunMultipleServers(t *testing.T) {
	count := 3
	for i := 0; i < count; i++ {
		server, err := startSSHServer(nil, nil)
		if err != nil {
			t.Fatalf("failed to start ssh server #%d: %v", i, err)
		}
		defer server.stop()
	}
}

func TestConnectAndClose(t *testing.T) {
	server, err := startSSHServer(nil, nil)
	if err != nil {
		t.Fatalf("failed to start ssh server: %v", err)
	}
	defer server.stop()

	client, err := NewClient(context.Background(), server.addr, server.clientConfig, DefaultConnectBackoff())
	if err != nil {
		t.Errorf("failed to connect: %v", err)
	}

	client.Close()
}
