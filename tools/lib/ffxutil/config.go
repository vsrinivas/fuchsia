// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ffxutil

import (
	"fmt"
	"os"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
)

// FFXConfig describes a config to run ffx with.
type FFXConfig struct {
	socket string
	config map[string]interface{}
	env    []string
}

// newIsolatedFFXConfig creates a config that provides an isolated environment to run ffx in.
func newIsolatedFFXConfig(dir string) *FFXConfig {
	socketPath := filepath.Join(dir, "ffx_socket")
	// TODO(fxbug.dev/64499): Stop setting environment variables once bug is fixed.
	// The env vars to set come from //src/developer/ffx/plugins/self-test/src/test/mod.rs.
	// TEMP, TMP, HOME, and XDG_CONFIG_HOME are set in //tools/lib/environment/environment.go
	// with a call to environment.Ensure().
	envMap := map[string]string{
		"ASCENDD": socketPath,
	}
	var env []string
	for k, v := range envMap {
		env = append(env, fmt.Sprintf("%s=%s", k, v))
	}
	config := &FFXConfig{
		socket: socketPath,
		config: make(map[string]interface{}),
		env:    env,
	}
	config.Set("overnet", map[string]string{"socket": socketPath})
	config.Set("log", map[string][]string{"dir": {filepath.Join(dir, "logs")}})
	config.Set("test", map[string][]string{"output_path": {filepath.Join(dir, "saved_test_runs")}})
	config.Set("fastboot", map[string]map[string]bool{"usb": {"disabled": true}})
	config.Set("ffx", map[string]map[string]bool{"analytics": {"disabled": true}})
	return config
}

// Env returns the environment that should be set when running ffx with this config.
func (c *FFXConfig) Env() []string {
	return c.env
}

// Set sets values in the config.
func (c *FFXConfig) Set(key string, value interface{}) {
	c.config[key] = value
}

// ToFile writes the config to a file.
func (c *FFXConfig) ToFile(configPath string) error {
	if err := os.MkdirAll(filepath.Dir(configPath), os.ModePerm); err != nil {
		return err
	}
	return jsonutil.WriteToFile(configPath, c.config)
}

// Close removes the socket if it hasn't been removed by `ffx daemon stop`.
func (c *FFXConfig) Close() error {
	if _, err := os.Stat(c.socket); !os.IsNotExist(err) {
		return os.Remove(c.socket)
	}
	return nil
}
