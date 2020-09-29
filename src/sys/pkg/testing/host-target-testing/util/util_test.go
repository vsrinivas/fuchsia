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

func TestParseCompat(t *testing.T) {
	tests := []struct {
		id   string
		list string
		json []byte
	}{
		{
			id:   "single package match",
			list: "abc/0=def\n",
			json: []byte(fmt.Sprintf(`{"version":1,"content":["%s"]}`, pkgURL("abc/0", "def"))),
		},
		{
			id:   "multiple package match",
			list: "abc/0=def\nghi/0=jkl",
			json: []byte(fmt.Sprintf(`{"version":1,"content":["%s","%s"]}`, pkgURL("abc/0", "def"), pkgURL("ghi/0", "jkl"))),
		},
		{
			id:   "order immaterial",
			list: "ghi/0=jkl\nabc/0=def",
			json: []byte(fmt.Sprintf(`{"version":1,"content":["%s","%s"]}`, pkgURL("abc/0", "def"), pkgURL("ghi/0", "jkl"))),
		},
		{
			id:   "variant-less match",
			list: "abc=def\n",
			json: []byte(fmt.Sprintf(`{"version":1,"content":["%s"]}`, pkgURL("abc", "def"))),
		},
	}

	for _, test := range tests {
		r := bytes.NewBufferString(test.list)
		listMap, err := ParsePackageList(r)
		if err != nil {
			t.Errorf("test %s: failed to parse package list", test.id)
		}

		r = bytes.NewBuffer(test.json)
		jsonMap, err := ParsePackagesJSON(r)
		if err != nil {
			t.Errorf("test %s: failed to parse packages.json", test.id)
		}

		if len(jsonMap) != len(listMap) {
			t.Errorf("test %s: got %d packages from json; want %d from list", test.id, len(jsonMap), len(listMap))
		}

		for k, v := range jsonMap {
			if lv, ok := listMap[k]; !ok {
				t.Errorf("test %s: got key %s in json; not found in list", test.id, k)
			} else if v != lv {
				t.Errorf("test %s: got value %s for key %s in json, want %s from list", test.id, v, k, lv)
			}
		}
	}
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
