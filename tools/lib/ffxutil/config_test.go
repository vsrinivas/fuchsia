// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ffxutil

import (
	"fmt"
	"os"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestFFXConfig(t *testing.T) {
	tmpDir := t.TempDir()
	config := newIsolatedFFXConfig(tmpDir)

	expectedSocket := filepath.Join(tmpDir, "ffx_socket")
	if config.socket != expectedSocket {
		t.Errorf("want socket at %s, got: %s", expectedSocket, config.socket)
	}
	config.Set("key", "value")
	expectedConfig := map[string]interface{}{
		"overnet": map[string]string{
			"socket": expectedSocket,
		},
		"log": map[string][]string{
			"dir": {filepath.Join(tmpDir, "logs")},
		},
		"test": map[string][]string{
			"output_path": {filepath.Join(tmpDir, "saved_test_runs")},
		},
		"fastboot": map[string]map[string]bool{
			"usb": {"disabled": true},
		},
		"ffx": map[string]map[string]bool{
			"analytics": {"disabled": true},
		},
		"key": "value",
	}
	if diff := cmp.Diff(expectedConfig, config.config); diff != "" {
		t.Errorf("Got wrong config (-want +got):\n%s", diff)
	}
	containsSocketPath := false
	for _, envvar := range config.env {
		if envvar == fmt.Sprintf("ASCENDD=%s", expectedSocket) {
			containsSocketPath = true
			break
		}
	}
	if !containsSocketPath {
		t.Errorf("Missing socket path in env; want ASCENDD=%s, got: %v", expectedSocket, config.env)
	}
	if err := config.ToFile(filepath.Join(tmpDir, "ffx_config")); err != nil {
		t.Errorf("failed to write config to file: %s", err)
	}
	if err := config.Close(); err != nil {
		t.Errorf("failed to close config: %s", err)
	}
	if _, err := os.Stat(config.socket); !os.IsNotExist(err) {
		t.Errorf("failed to remove socket %s, err: %s", config.socket, err)
	}
}
