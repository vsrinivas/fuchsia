// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

import (
	"bytes"
	"fmt"
	"testing"
)

func pkgURL(name, hash string) string {
	return fmt.Sprintf("fuchsia-pkg://host/%s?hash=%s", name, hash)
}

type pkgMerkle struct {
	name string
	hash string
}

func TestParsePackagesJSON(t *testing.T) {
	tests := []struct {
		id        string
		json      []byte
		pkgs      []pkgMerkle
		expectErr bool
	}{
		{
			id:        "invalid json fails",
			json:      []byte("abcd"),
			expectErr: true,
		},
		{
			id:        "empty object fails",
			json:      []byte("{}"),
			expectErr: true,
		},
		{
			id:        "invalid version fails",
			json:      []byte(`{"version":"42"}`),
			expectErr: true,
		},
		{
			id:        "numeric, contentless version succeeds",
			json:      []byte(`{"version":1}`),
			expectErr: false,
		},
		{
			id:        "variant-less package succeeds",
			json:      []byte(fmt.Sprintf(`{"version":1,"content":["%s"]}`, pkgURL("pkg", "abc"))),
			pkgs:      []pkgMerkle{{"pkg", "abc"}},
			expectErr: false,
		},
		{
			id:        "variant package succeeds",
			json:      []byte(fmt.Sprintf(`{"version":1,"content":["%s"]}`, pkgURL("pkg/0", "abc"))),
			pkgs:      []pkgMerkle{{"pkg/0", "abc"}},
			expectErr: false,
		},
		{
			id:        "multiple packages succeed",
			json:      []byte(fmt.Sprintf(`{"version":1,"content":["%s","%s"]}`, pkgURL("pkg/0", "abc"), pkgURL("another/0", "def"))),
			pkgs:      []pkgMerkle{{"pkg/0", "abc"}, {"another/0", "def"}},
			expectErr: false,
		},
	}

	for _, test := range tests {
		r := bytes.NewBuffer(test.json)
		pkgs, err := ParsePackagesJSON(r)

		gotErr := (err != nil)
		if test.expectErr && !gotErr {
			t.Errorf("test %s: want error; got none", test.id)
		} else if !test.expectErr && gotErr {
			t.Errorf("test %s: want no error; got %v", test.id, err)
		}

		if len(pkgs) != len(test.pkgs) {
			t.Errorf("test %s: got %d packages; want %d", test.id, len(pkgs), len(test.pkgs))
		}

		if test.pkgs != nil {
			for _, pv := range test.pkgs {
				h, ok := pkgs[pv.name]
				if !ok {
					t.Errorf("test %s: key not found; want %s", test.id, pv.name)
				}
				if h != pv.hash {
					t.Errorf("test %s for package %s: got hash %s; want %s", test.id, pv.name, h, pv.hash)
				}
			}
		}
	}
}
