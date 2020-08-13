// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"reflect"
	"strings"
	"testing"
)

func TestLoad(t *testing.T) {
	index, err := LoadIndex(strings.NewReader(`/absolute/path/to/symbol	/some/build/dir
/another/absolute/path
`))
	if err != nil {
		t.Fatal(err)
	}
	want := Index{
		Entry{SymbolPath: "/absolute/path/to/symbol", BuildDir: "/some/build/dir"},
		Entry{SymbolPath: "/another/absolute/path"},
	}
	if !reflect.DeepEqual(index, want) {
		t.Error("got", index, "expected", want)
	}
}
