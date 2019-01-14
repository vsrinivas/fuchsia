// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"strings"
	"testing"
)

func TestSetAndGetDeviceContext(t *testing.T) {
	newDeviceContext := func() *DeviceContext {
		return &DeviceContext{
			Nodename: "nodename",
			SSHKey:   "private-key",
		}

	}

	t.Run("context can be registered and unregistered", func(t *testing.T) {
		devCtx := newDeviceContext()
		if err := devCtx.Register(); err != nil {
			t.Fatal(err)
		}
		envEntry := devCtx.EnvironEntry()
		tokens := strings.SplitAfterN(envEntry, "=", 2)
		if len(tokens) == 1 {
			t.Errorf("%s did not contain a \"=\"", envEntry)
		} else if value := tokens[1]; value == "" {
			t.Error("device context environment entry value is empty")
		}
		if err := devCtx.Unregister(); err != nil {
			t.Fatal(err)
		}
	})
}
