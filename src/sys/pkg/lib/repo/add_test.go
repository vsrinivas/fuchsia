// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !fuchsia

package repo

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"reflect"
	"testing"
)

type mockShell struct {
	writer *mockWriteCloser
	cmds   [][]string
}

func (sh *mockShell) writerAt(_ string) (io.WriteCloser, error) {
	return sh.writer, nil
}

func (sh *mockShell) run(_ context.Context, cmd []string) error {
	sh.cmds = append(sh.cmds, cmd)
	return nil
}

type mockWriteCloser struct {
	buff   bytes.Buffer
	closed bool
}

func (w *mockWriteCloser) Write(p []byte) (int, error) {
	return w.buff.Write(p)
}

func (w *mockWriteCloser) Close() error {
	w.closed = true
	return nil
}

func TestAddFromConfig(t *testing.T) {
	ctx := context.Background()

	cfg := &Config{
		URL: "fuchsia-pkg://example.com",
		RootKeys: []KeyConfig{
			{
				ED25519Key: "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100",
			},
		},
		Mirrors: []MirrorConfig{{
			Subscribe: true,
		}},
	}

	sh := &mockShell{
		writer: &mockWriteCloser{},
	}
	installPath := "/path/on/remote/filesystem"

	// This first call should fail, as the mirror does not specify a URL.
	if err := addFromConfig(ctx, cfg, sh, installPath); err == nil {
		t.Errorf("succeeded when we should have failed; mirrors need URLs")
	}

	// Set the mirror URL and try again.
	cfg.Mirrors[0].URL = "https://example.com/repo"
	if err := addFromConfig(ctx, cfg, sh, installPath); err != nil {
		t.Fatalf("failed to configure: %v", err)
	}

	if len(sh.cmds) == 0 || len(sh.cmds) > 1 {
		t.Errorf("%d commands run; expected 1", len(sh.cmds))
	} else if !reflect.DeepEqual(sh.cmds[0], repoAddCmd(installPath)) {
		t.Errorf("expected: %v;\nactual:%v", repoAddCmd(installPath), sh.cmds[0])
	}
	if !sh.writer.closed {
		t.Errorf("writer was not closed")
	}

	actualBytes, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		t.Fatalf("could not marshal repository config: %v", err)
	}
	if bytes.Compare(actualBytes, sh.writer.buff.Bytes()) != 0 {
		t.Errorf("config not transported as expected")
	}
}
