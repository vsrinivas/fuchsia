// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"bytes"
	"reflect"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
)

func TestInstanceHandle(t *testing.T) {
	// Instance loading automatically loads the build, so we need to stub it out
	realNewBuild := NewBuild
	NewBuild = newMockBuild
	defer func() { NewBuild = realNewBuild }()

	launcher := &QemuLauncher{Pid: 404, TmpDir: "/some/dir"}
	connector := &SSHConnector{Host: "somehost", Port: 123, Key: "keyfile"}
	build, _ := newMockBuild()
	instance := &BaseInstance{Build: build, Launcher: launcher, Connector: connector}

	handle, err := instance.Handle()
	if err != nil {
		t.Fatalf("error creating handle: %s", err)
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
}

func TestInstanceRepeatedStart(t *testing.T) {
	build, _ := newMockBuild()
	launcher := &mockLauncher{}
	i := &BaseInstance{Build: build, Launcher: launcher}

	if err := i.Start(); err != nil {
		t.Fatalf("Error starting instance: %s", err)
	}

	if err := i.Start(); err == nil {
		t.Fatalf("Expected error starting already-running instance but succeeded")
	}
}

func TestInstanceStartWithLauncherFailure(t *testing.T) {
	build, _ := newMockBuild()
	launcher := &mockLauncher{shouldFailToStart: true}
	i := &BaseInstance{Build: build, Launcher: launcher}

	if err := i.Start(); err == nil {
		t.Fatalf("Expected launcher failure but succeeded")
	}
}

func TestInstanceStartWithConnectorFailure(t *testing.T) {
	build, _ := newMockBuild()
	launcher := &mockLauncher{shouldFailToConnect: true}
	i := &BaseInstance{Build: build, Launcher: launcher}

	if err := i.Start(); err == nil {
		t.Fatalf("Expected connector failure but succeeded")
	}
}

func TestInstance(t *testing.T) {
	build, _ := newMockBuild()
	launcher := &mockLauncher{}
	i := &BaseInstance{Build: build, Launcher: launcher}

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

	expected := []string{"/data/r/sys/fuchsia.com:foo:0#meta:bar.cmx/path/to/remoteFile"}
	got := i.Connector.(*mockConnector).PathsGot
	if !reflect.DeepEqual(got, expected) {
		t.Fatalf("incorrect file get list: %v", got)
	}

	remotePath = "data/path/to/otherFile"
	if err := i.Put("foo/bar", "/path/to/localFile", remotePath); err != nil {
		t.Fatalf("Error putting to instance: %s", err)
	}

	expected = []string{"/data/r/sys/fuchsia.com:foo:0#meta:bar.cmx/path/to/otherFile"}
	got = i.Connector.(*mockConnector).PathsPut
	if !reflect.DeepEqual(got, expected) {
		t.Fatalf("incorrect file put list: %v", got)
	}

	if err := i.Stop(); err != nil {
		t.Fatalf("Error stopping instance: %s", err)
	}
}

func TestInstanceRunFuzzerWithArtifactFetch(t *testing.T) {
	build, _ := newMockBuild()
	launcher := &mockLauncher{}
	i := &BaseInstance{Build: build, Launcher: launcher}

	if err := i.Start(); err != nil {
		t.Fatalf("Error starting instance: %s", err)
	}

	hostArtifactDir := "/art/dir"
	var outBuf bytes.Buffer
	if err := i.RunFuzzer(&outBuf, "foo/bar", hostArtifactDir, "-artifact_prefix=data/wow/x"); err != nil {
		t.Fatalf("Error running fuzzer: %s", err)
	}

	out := outBuf.String()
	if !strings.Contains(out, "/art/dir/xcrash-1312") {
		t.Fatalf("fuzzer output missing host artifact path: %q", out)
	}

	expected := []string{"/data/r/sys/fuchsia.com:foo:0#meta:bar.cmx/wow/xcrash-1312"}
	got := i.Connector.(*mockConnector).PathsGot
	if !reflect.DeepEqual(got, expected) {
		t.Fatalf("incorrect file get list: %v", got)
	}

	if err := i.Stop(); err != nil {
		t.Fatalf("Error stopping instance: %s", err)
	}
}
