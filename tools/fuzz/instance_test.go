// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"bytes"
	"reflect"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
)

func newTestInstance() *BaseInstance {
	build, _ := newMockBuild()
	return &BaseInstance{Build: build, reconnectInterval: defaultReconnectInterval}
}

func TestInstanceHandle(t *testing.T) {
	// Instance loading automatically loads the build, so we need to stub it out
	realNewBuild := NewBuild
	NewBuild = newMockBuild
	defer func() { NewBuild = realNewBuild }()

	instance := newTestInstance()
	instance.Launcher = &QemuLauncher{Pid: 404, TmpDir: "/some/dir"}
	instance.Connector = NewSSHConnector("somehost", 123, "keyfile")

	handle, err := instance.Handle()
	if err != nil {
		t.Fatalf("error creating handle: %s", err)
	}
	defer handle.Release()

	handle2, err := instance.Handle()
	if handle.Serialize() != handle2.Serialize() {
		handle2.Release()
		t.Fatalf("same instance returned different handles: %q, %q", handle, handle2)
	}

	// Note: we don't serialize here because that is covered by handle tests

	reloadedInstance, err := loadInstanceFromHandle(handle)
	if err != nil {
		t.Fatalf("error loading instance from handle: %s", err)
	}

	got, ok := reloadedInstance.(*BaseInstance)
	if !ok {
		t.Fatalf("incorrect instance type")
	}

	if diff := cmp.Diff(instance, got, cmpopts.IgnoreUnexported(BaseInstance{},
		SSHConnector{}, QemuLauncher{}, mockBuild{})); diff != "" {
		t.Fatalf("incorrect data in reloaded instance (-want +got):\n%s", diff)
	}

	// Ensure that handle is released and invalid upon stopping
	if err := instance.Stop(); err != nil {
		t.Fatalf("error stopping instance: %s", err)
	}

	reloadedInstance, err = loadInstanceFromHandle(handle)
	if err == nil {
		t.Fatalf("unexpected success reloading instance from released handle: %q", handle)
	}
}

func TestInstanceRepeatedStart(t *testing.T) {
	i := newTestInstance()
	i.Launcher = &mockLauncher{}

	if err := i.Start(); err != nil {
		t.Fatalf("Error starting instance: %s", err)
	}

	if err := i.Start(); err == nil {
		t.Fatalf("Expected error starting already-running instance but succeeded")
	}
}

func TestInstanceStartWithLauncherFailure(t *testing.T) {
	i := newTestInstance()
	i.Launcher = &mockLauncher{shouldFailToStart: true}

	if err := i.Start(); err == nil {
		t.Fatalf("expected launcher failure but succeeded")
	}
}

func TestInstanceStartWithLauncherEarlyExit(t *testing.T) {
	i := newTestInstance()
	// Only the first connection will fail, but no retries should be made
	// because the launcher will have died.
	i.Launcher = &mockLauncher{shouldFailToConnectCount: 1, shouldExitEarly: true}

	if err := i.Start(); err == nil {
		t.Fatalf("expected launcher failure but succeeded")
	}
}

func TestInstanceStartWithTemporaryConnectorFailure(t *testing.T) {
	i := newTestInstance()
	i.Launcher = &mockLauncher{shouldFailToConnectCount: maxInitialConnectAttempts - 1}
	i.reconnectInterval = 1 * time.Millisecond

	if err := i.Start(); err != nil {
		t.Fatalf("Expected connector to succeed after reconnection attempt")
	}

	if err := i.Stop(); err != nil {
		t.Fatalf("Error stopping instance: %s", err)
	}

	if running, _ := i.Launcher.IsRunning(); running {
		t.Fatalf("expected launcher to have been killed, but it is running")
	}
}

func TestInstanceStartWithPermanentConnectorFailure(t *testing.T) {
	i := newTestInstance()
	i.Launcher = &mockLauncher{shouldFailToConnectCount: maxInitialConnectAttempts}
	i.reconnectInterval = 1 * time.Millisecond

	if err := i.Start(); err == nil {
		t.Fatalf("Expected connector failure but succeeded")
	}

	if running, _ := i.Launcher.IsRunning(); running {
		t.Fatalf("expected launcher to have been killed, but it is running")
	}
}

func TestInstanceStartWithTemporaryCommandFailure(t *testing.T) {
	i := newTestInstance()
	i.Launcher = &mockLauncher{shouldFailToExecuteCount: maxInitialConnectAttempts - 1}
	i.reconnectInterval = 1 * time.Millisecond

	if err := i.Start(); err != nil {
		t.Fatalf("Expected to succeed after command re-execution attempt")
	}

	if err := i.Stop(); err != nil {
		t.Fatalf("Error stopping instance: %s", err)
	}

	if running, _ := i.Launcher.IsRunning(); running {
		t.Fatalf("expected launcher to have been killed, but it is running")
	}
}

func TestInstanceStartWithPermanentCommandFailure(t *testing.T) {
	i := newTestInstance()
	i.Launcher = &mockLauncher{shouldFailToExecuteCount: maxInitialConnectAttempts + 1}
	i.reconnectInterval = 1 * time.Millisecond

	if err := i.Start(); err == nil {
		t.Fatalf("Expected command execution failure but succeeded")
	}

	if running, _ := i.Launcher.IsRunning(); running {
		t.Fatalf("expected launcher to have been killed, but it is running")
	}
}

func TestInstance(t *testing.T) {
	i := newTestInstance()
	i.Launcher = &mockLauncher{}

	if err := i.Start(); err != nil {
		t.Fatalf("Error starting instance: %s", err)
	}

	fuzzers := i.ListFuzzers()
	if !reflect.DeepEqual(fuzzers, []string{"foo/bar", "fail/nopid", "fail/notfound"}) {
		t.Fatalf("incorrect fuzzer list: %v", fuzzers)
	}

	var outBuf bytes.Buffer
	if err := i.RunFuzzer(&outBuf, "foo/bar", "", "arg1", "arg2"); err != nil {
		t.Fatalf("Error running fuzzer: %s", err)
	}
	out := outBuf.String()

	// Just a basic check here, more are done in fuzzer_tests
	if !strings.Contains(out, "fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx") {
		t.Fatalf("fuzzer output missing package: %q", out)
	}

	if err := i.RunFuzzer(&outBuf, "invalid/fuzzer", ""); err == nil {
		t.Fatalf("expected error when running invalid fuzzer")
	}

	remotePath := "data/path/to/remoteFile"
	if err := i.Get("foo/bar", remotePath, "/path/to/localFile"); err != nil {
		t.Fatalf("Error getting from instance: %s", err)
	}

	expected := []string{"/tmp/r/sys/fuchsia.com:foo:0#meta:bar.cmx/path/to/remoteFile"}
	got := i.Connector.(*mockConnector).PathsGot
	if !reflect.DeepEqual(got, expected) {
		t.Fatalf("incorrect file get list: %v", got)
	}

	remotePath = "data/path/to/otherFile"
	if err := i.Put("foo/bar", "/path/to/localFile", remotePath); err != nil {
		t.Fatalf("Error putting to instance: %s", err)
	}

	expected = []string{"/tmp/r/sys/fuchsia.com:foo:0#meta:bar.cmx/path/to/otherFile"}
	got = i.Connector.(*mockConnector).PathsPut
	if !reflect.DeepEqual(got, expected) {
		t.Fatalf("incorrect file put list: %v", got)
	}

	var logBuf bytes.Buffer
	if err := i.GetLogs(&logBuf); err != nil {
		t.Fatalf("error getting instance logs: %s", err)
	}

	log := logBuf.String()
	if log != "system log\n" {
		t.Fatalf("unexpected instance log: %q", log)
	}

	if err := i.Stop(); err != nil {
		t.Fatalf("Error stopping instance: %s", err)
	}

	if running, _ := i.Launcher.IsRunning(); running {
		t.Fatalf("expected launcher to have been killed, but it is running")
	}
}

func TestInstanceRunFuzzerWithArtifactFetch(t *testing.T) {
	i := newTestInstance()
	i.Launcher = &mockLauncher{}

	if err := i.Start(); err != nil {
		t.Fatalf("Error starting instance: %s", err)
	}

	hostArtifactDir := "/art/dir"
	var outBuf bytes.Buffer
	args := []string{"-artifact_prefix=data/wow/x", "data/corpus"}
	if err := i.RunFuzzer(&outBuf, "foo/bar", hostArtifactDir, args...); err != nil {
		t.Fatalf("Error running fuzzer: %s", err)
	}

	out := outBuf.String()
	if !strings.Contains(out, "/art/dir/xcrash-1312") {
		t.Fatalf("fuzzer output missing host artifact path: %q", out)
	}

	expected := []string{"/tmp/r/sys/fuchsia.com:foo:0#meta:bar.cmx/wow/xcrash-1312"}
	got := i.Connector.(*mockConnector).PathsGot
	if !reflect.DeepEqual(got, expected) {
		t.Fatalf("incorrect file get list: %v", got)
	}

	if err := i.Stop(); err != nil {
		t.Fatalf("Error stopping instance: %s", err)
	}

	if running, _ := i.Launcher.IsRunning(); running {
		t.Fatalf("expected launcher to have been killed, but it is running")
	}
}
