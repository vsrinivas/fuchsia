// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sshutil

import (
	"context"
	"testing"
)

func TestRunMultipleServers(t *testing.T) {
	const count = 3
	var servers []*sshServer
	defer func() {
		for i := range servers {
			if err := servers[i].stop(); err != nil {
				t.Errorf("servers[%d].stop() = %s", i, err)
			}
		}
	}()
	for i := 0; i < count; i++ {
		server, err := startSSHServer(context.Background(), nil, nil)
		if err != nil {
			t.Fatalf("failed to start ssh server #%d: %s", i, err)
		}
		servers = append(servers, server)
	}
}

func TestConnectAndClose(t *testing.T) {
	server, err := startSSHServer(context.Background(), nil, nil)
	if err != nil {
		t.Fatalf("failed to start ssh server: %s", err)
	}
	defer func() {
		if err := server.stop(); err != nil {
			t.Errorf("server.stop() = %s", err)
		}
	}()

	client, err := NewClient(
		context.Background(),
		ConstantAddrResolver{
			Addr: server.listener.Addr(),
		},
		server.clientConfig,
		DefaultConnectBackoff(),
	)
	if err != nil {
		t.Errorf("failed to connect: %s", err)
	}

	client.Close()
}
