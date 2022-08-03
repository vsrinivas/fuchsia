// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Package environment ensures correct environment is available.
package environment

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func newFakeEnv() fakeEnv {
	return fakeEnv{values: make(map[string]string)}
}

func (f fakeEnv) getenv(k string) string {
	return f.values[k]
}

func (f fakeEnv) lookupEnv(k string) (string, bool) {
	v, ok := f.values[k]
	return v, ok
}

func (f fakeEnv) setenv(k, v string) error {
	f.values[k] = v
	return nil
}

type fakeEnv struct {
	values map[string]string
}

func TestEnsure(t *testing.T) {
	cases := []struct {
		name        string
		isolated    bool
		env         map[string]string
		expectedEnv map[string]string
	}{
		{
			name:     "ensure with tmp unset and isolated",
			isolated: true,
		},
		{
			name: "ensure with tmp unset and not isolated",
		},
		{
			name:     "ensure with tmp set and isolated",
			isolated: true,
			env:      map[string]string{"TMPDIR": "foo"},
		},
		{
			name:        "ensure with tmp set and not isolated",
			env:         map[string]string{"TMPDIR": "foo"},
			expectedEnv: map[string]string{"TMPDIR": "foo"},
		},
		{
			name:     "ensure with other set and isolated",
			isolated: true,
			env:      map[string]string{"TMPDIR": "foo", "ANDROID_TMP": "bar"},
		},
		{
			name:        "ensure with other set and not isolated",
			env:         map[string]string{"TMPDIR": "foo", "ANDROID_TMP": "bar"},
			expectedEnv: map[string]string{"TMPDIR": "foo", "ANDROID_TMP": "bar"},
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			e := newFakeEnv()
			for k, v := range tc.env {
				e.setenv(k, v)
			}
			cleanUp, err := ensure(e, tc.isolated)
			if err != nil {
				t.Fatalf("ensure() failed: %s", err)
			}
			if cleanUp == nil {
				t.Fatalf("ensure() returned unexpectedly nil cleanup func")
			}
			defer cleanUp()
			v, ok := e.lookupEnv("TMPDIR")
			if !ok {
				t.Errorf("$TMPDIR unset")
			}
			if expected, ok := tc.expectedEnv["TMPDIR"]; ok {
				if diff := cmp.Diff(v, expected); diff != "" {
					t.Errorf("$TMPDIR -got, +want: %s", diff)
				}
			} else if orig, ok := tc.env["TMPDIR"]; ok {
				if diff := cmp.Diff(v, orig); diff == "" {
					t.Errorf("got $TMPDIR=%s, want new temp dir", v)
				}
			}

			for _, envVar := range TempDirEnvVars() {
				v, ok := e.lookupEnv(envVar)
				if !ok {
					t.Errorf("$%s unset", envVar)
				}
				expected, ok := tc.expectedEnv[envVar]
				if !ok {
					expected = e.values["TMPDIR"]
				}
				if diff := cmp.Diff(v, expected); diff != "" {
					t.Errorf("$%s -got, +want: %s", envVar, diff)
				}
			}
		})
	}
}
