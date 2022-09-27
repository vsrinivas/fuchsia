// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package suspend

import (
	"flag"
	"os"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/cli"
)

type config struct {
	deviceConfig *cli.DeviceConfig
}

func newConfig(fs *flag.FlagSet) (*config, error) {
	// test_data/suspend-test corresponds to the host tools output path that we configure in the
	// build file.
	testDataPath := filepath.Join(filepath.Dir(os.Args[0]), "test_data", "suspend-test")
	deviceConfig := cli.NewDeviceConfig(fs, testDataPath)

	c := &config{
		deviceConfig: deviceConfig,
	}

	return c, nil
}
