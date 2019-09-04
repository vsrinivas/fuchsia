// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"bytes"
	"fmt"
	"testing"
)

func TestRegexTokenize(t *testing.T) {
	var builder regexpTokenizerBuilder
	var out bytes.Buffer
	builder.addRule("{a+}", func(args ...string) {
		fmt.Fprintf(&out, "a+ case: %v\n", args)
	})
	builder.addRule("{b+}", func(args ...string) {
		fmt.Fprintf(&out, "b+ case: %v\n", args)
	})
	builder.addRule("{(x)(y)(z)}", func(args ...string) {
		fmt.Fprintf(&out, "xyz case: %v\n", args)
	})
	tokenizer, err := builder.compile(func(str string) {
		fmt.Fprintf(&out, "default case: %s\n", str)
	})
	if err != nil {
		t.Fatal(err)
	}
	tokenizer.run("blarg{a}foo{bbbbb}{xyz}{aa}{aa}baz[test]rest")
	expected := `default case: blarg
a+ case: [{a}]
default case: foo
b+ case: [{bbbbb}]
xyz case: [{xyz} x y z]
a+ case: [{aa}]
a+ case: [{aa}]
default case: baz[test]rest
`
	actual := string(out.Bytes())
	if expected != string(out.Bytes()) {
		t.Error("expected\n", expected, "\ngot\n", actual)
	}
}
