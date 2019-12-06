// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

// +build !fuchsia

package repo

import (
	"bytes"
	"encoding/json"
	"io"
	"testing"
)

type mockShell struct {
	writer *mockWriteCloser
	cmds   []string
}

func (sh *mockShell) writerAt(installPath string) (io.WriteCloser, error) {
	return sh.writer, nil
}

func (sh *mockShell) run(cmd string) error {
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
	cfg := &Config{
		URL: "fuchsia-pkg://example.com",
		RootKeys: []KeyConfig{
			{
				ED25519Key: "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100",
			},
		},
		Mirrors: []MirrorConfig{{
			URL:       "https://example.com/repo",
			Subscribe: true,
		}},
	}

	sh := &mockShell{
		writer: &mockWriteCloser{},
	}

	installPath := "/path/on/remote/filesystem"
	if err := addFromConfig(cfg, installPath, sh); err != nil {
		t.Fatalf("failed to configure: %v", err)
	}
	if len(sh.cmds) == 0 || len(sh.cmds) > 1 {
		t.Errorf("%d commands run; expected 1", len(sh.cmds))
	} else if sh.cmds[0] != repoAddCmd(installPath) {
		t.Errorf("expected: %s;\nactual:%s", repoAddCmd(installPath), sh.cmds[0])
	}
	if !sh.writer.closed {
		t.Errorf("writer was not closed")
	}

	actualBytes, err := json.Marshal(cfg)
	if err != nil {
		t.Fatalf("could not marshal repository config: %v", err)
	}
	if bytes.Compare(actualBytes, sh.writer.buff.Bytes()) != 0 {
		t.Errorf("config not transported as expected")
	}
}
