// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"os"
	"path/filepath"
	"testing"

	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"google.golang.org/protobuf/encoding/prototext"
	"google.golang.org/protobuf/proto"
)

func TestReadStatic(t *testing.T) {
	message := &fintpb.Static{
		Optimize:         fintpb.Static_RELEASE,
		Board:            "qemu",
		Product:          "workstation",
		NinjaTargets:     []string{"default"},
		IncludeHostTests: false,
		TargetArch:       fintpb.Static_X64,
		IncludeArchives:  false,
		SkipIfUnaffected: true,
	}

	path := filepath.Join(t.TempDir(), "static.textproto")
	writeTextproto(t, path, message)

	got, err := ReadStatic(path)
	if err != nil {
		t.Errorf("Failed to read static.textproto: %s", err)
	}
	if diff := cmp.Diff(message, got, cmpopts.IgnoreUnexported(fintpb.Static{})); diff != "" {
		t.Fatalf("Unexpected diff reading Static (-want +got):\n%s", diff)
	}
}

func TestReadContext(t *testing.T) {
	message := &fintpb.Context{
		CheckoutDir: "/a/b/c/fuchsia/",
		BuildDir:    "/a/b/c/fuchsia/out/release",
		ArtifactDir: "/a/b/c/fuchsia/out/artifacts",
	}

	path := filepath.Join(t.TempDir(), "context.textproto")
	writeTextproto(t, path, message)

	got, err := ReadContext(path)
	if err != nil {
		t.Errorf("Failed to read context.textproto: %s", err)
	}
	if diff := cmp.Diff(message, got, cmpopts.IgnoreUnexported(fintpb.Context{})); diff != "" {
		t.Fatalf("Unexpected diff reading Static (-want +got):\n%s", diff)
	}
}

func writeTextproto(t *testing.T, path string, message proto.Message) {
	t.Helper()

	f, err := osmisc.CreateFile(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	t.Cleanup(func() { os.Remove(path) })

	b, err := prototext.Marshal(message)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := f.Write(b); err != nil {
		t.Fatal(err)
	}
}
