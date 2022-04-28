// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ffxutil

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func diffConfig(config1, config2 map[string]interface{}) string {
	return cmp.Diff(
		config1,
		config2,
		// Without this, map[string]interface{} might compare differently to e.g.
		// map[string](map[string]interface{}) even when they are semantically equivalent.
		cmp.Transformer("into_json", func(m map[string]interface{}) string {
			b, err := json.MarshalIndent(m, "", "  ")
			if err != nil {
				panic(err)
			}
			return string(b)
		}),
	)
}

func TestFFXConfig(t *testing.T) {
	tmpDir := t.TempDir()
	config := newIsolatedFFXConfig(tmpDir)

	expectedSocket := filepath.Join(tmpDir, "ffx_socket")
	if config.socket != expectedSocket {
		t.Errorf("want socket at %s, got: %s", expectedSocket, config.socket)
	}
	config.Set("key", "value")
	if logLevel := config.GetJsonPointer("/log/level"); logLevel != nil {
		t.Errorf("log level is unexpectedly already set: %v", logLevel)
	}
	config.SetJsonPointer("/log/level", "Trace")
	if logLevel := config.GetJsonPointer("/log/level"); logLevel != "Trace" {
		t.Errorf("GetJsonPointer returned %v, want Trace", logLevel)
	}
	expectedConfig := map[string]interface{}{
		"overnet": map[string]string{
			"socket": expectedSocket,
		},
		"log": map[string]interface{}{
			"dir":   []string{filepath.Join(tmpDir, "logs")},
			"level": "Trace",
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
	if diff := diffConfig(expectedConfig, config.config); diff != "" {
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
	configFilepath := filepath.Join(tmpDir, "ffx_config")
	if err := config.ToFile(configFilepath); err != nil {
		t.Errorf("failed to write config to file: %s", err)
	}
	configFromFile, err := configFromFile(configFilepath)
	if err != nil {
		t.Errorf("failed to read config from file %q: %s", configFilepath, err)
	}
	if diff := diffConfig(config.config, configFromFile.config); diff != "" {
		t.Errorf("config != configFromFile (-config +configFromFile):\n%s", diff)
	}
	if err := config.Close(); err != nil {
		t.Errorf("failed to close config: %s", err)
	}
	if _, err := os.Stat(config.socket); !os.IsNotExist(err) {
		t.Errorf("failed to remove socket %s, err: %s", config.socket, err)
	}
}
