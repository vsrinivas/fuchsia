// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package target

import (
	"io/ioutil"
	"path/filepath"
	"testing"
)

func TestLoadConfigs(t *testing.T) {
	tests := []struct {
		name        string
		jsonStr     string
		expectedLen int
		expectErr   bool
	}{
		// Valid configs.
		{"ValidConfig", `[{"nodename":"upper-drank-wick-creek"},{"nodename":"siren-swoop-wick-hasty"}]`, 2, false},
		// Invalid configs.
		{"InvalidConfig", `{{"nodename":"upper-drank-wick-creek"},{"nodename":"siren-swoop-wick-hasty"}}`, 0, true},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			configs, err := LoadDeviceConfigs(mkTempFile(t, test.jsonStr))
			if test.expectErr && err == nil {
				t.Error("expected errors; no errors found")
			}
			if !test.expectErr && err != nil {
				t.Errorf("expected no errors; found error %s", err)
			}
			if len(configs) != test.expectedLen {
				t.Errorf("expected %d nodes; found %d", test.expectedLen, len(configs))
			}
		})
	}
}

// mkTempFile returns a new temporary file with the specified content that will
// be cleaned up automatically.
func mkTempFile(t *testing.T, content string) string {
	name := filepath.Join(t.TempDir(), "foo")
	if err := ioutil.WriteFile(name, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	return name
}
