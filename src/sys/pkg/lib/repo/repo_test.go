// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package repo

import (
	"bytes"
	"encoding/json"
	"io"
	"reflect"
	"testing"
)

type mockShell struct {
	writer *mockWriteCloser
	cmds   []string
	closed bool
}

func (sh *mockShell) writerAt(remote string) (io.WriteCloser, error) {
	return sh.writer, nil
}

func (sh *mockShell) run(cmd string) error {
	sh.cmds = append(sh.cmds, cmd)
	return nil
}

func (sh *mockShell) close() error {
	sh.closed = true
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

func TestConfigurer(t *testing.T) {

	cfg := &RepositoryConfig{
		URL: "fuchsia-pkg://example.com",
		RootKeys: []RepositoryKeyConfig{
			{
				ED25519Key: "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100",
			},
		},
		Mirrors: []MirrorConfig{{
			URL: "https://example.com/repo",
			BlobKey: RepositoryBlobKey{
				AESKey: "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
			},
			Subscribe: true,
		}},
	}
	goldenCfgStr := `
    {
      "repo_url": "fuchsia-pkg://example.com",
      "root_keys": [
        {
          "type": "ed25519",
          "value": "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100"
        }
      ],
      "mirrors": [
        {
          "mirror_url": "https://example.com/repo",
          "blob_key": {
            "type": "aes",
            "value": "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"
          },
          "subscribe": true
        }
      ]
  }`

	sh := &mockShell{
		writer: &mockWriteCloser{},
	}
	configurer := Configurer{shell: sh}
	defer func() {
		configurer.Close()
		if !sh.closed {
			t.Fatalf("shell not closed")
		}
	}()

	remote := "/path/to/remote"
	if err := configurer.Configure(cfg, remote); err != nil {
		t.Fatalf("failed to configure: %v", err)
	}
	if len(sh.cmds) == 0 || len(sh.cmds) > 1 {
		t.Errorf("%d commands run; expected 1", len(sh.cmds))
	} else if sh.cmds[0] != repoAddCmd(remote) {
		t.Errorf("expected: %s;\nactual:%s", repoAddCmd(remote), sh.cmds[0])
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

	goldenCfg := &RepositoryConfig{}
	if err := json.Unmarshal([]byte(goldenCfgStr), goldenCfg); err != nil {
		t.Fatalf("could not unmarsal golden config string: %v", err)
	}
	if !reflect.DeepEqual(cfg, goldenCfg) {
		t.Fatalf("expected\n%#v\nand\n%#v\nto be equal", cfg, goldenCfg)
	}
}
