// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests that every combination of echo server and client can communicate.

package compatibility_test

import (
	"fmt"
	"os/exec"
	"testing"
	"time"
)

var servers = []string{
	"echo_server_cpp", "echo_server_go", "echo_server_rust"}
var clients = []string{
	"echo_client_cpp",	"echo_client_go",	"echo_client_rust"}

func TestCompatibility(t *testing.T) {
	for _, server := range servers {
		for _, client := range clients {
			fmt.Printf("Testing client %s and server %s\n", client, server)
			cmd := exec.Command("run", client, "--server", server)
			if err := cmd.Start(); err != nil {
				t.Errorf("Failed to start client %s", client)
				continue
			}
			// Handle stuckness.
			time.AfterFunc(5 * time.Second, func() {
					cmd.Process.Kill()
			})
			if err := cmd.Wait(); err != nil {
				t.Errorf("Server: %s\nClient: %s\nError: %v", server, client, err)
			}
		}
	}
}
