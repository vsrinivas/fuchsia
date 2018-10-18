// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_ota

import (
	"testing"
)

const (
	remotePackageDataPath = "/pkgfs/packages/ota-test-package/0/bin/app"
	remoteSystemDataPath  = "/system/bin/ota-test-app"
)

func TestPackageInstall(t *testing.T) {
	PrepareOTA(t)

	// Verify the packages are not installed.
	if RemoteFileExists(t, remotePackageDataPath) {
		t.Fatalf("%q should not exist", remotePackageDataPath)
	}
	if RemoteFileExists(t, remoteSystemDataPath) {
		t.Fatalf("%q should not exist", remoteSystemDataPath)
	}

	// Actually start the OTA.
	TriggerOTA(t)

	// Verify the packages are installed.
	if !RemoteFileExists(t, remotePackageDataPath) {
		t.Fatalf("%q should exist", remotePackageDataPath)
	}
	if !RemoteFileExists(t, remoteSystemDataPath) {
		t.Fatalf("%q should exist", remoteSystemDataPath)
	}
}
