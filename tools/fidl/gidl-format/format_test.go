// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
)

var cases = []struct {
	input, output string
}{
	{input: "", output: "\n"},
	{input: " ", output: "\n"},
	{input: "\n", output: "\n"},
	{input: " \r\n\t", output: "\n"},
	{input: " foo ", output: "foo\n"},
	{input: "foo=0x42", output: "foo = 0x42\n"},
	{input: "foo { }", output: "foo{}\n"},
	{input: "( \r\n\t)", output: "()\n"},
	{input: "[ \r\n\t]", output: "[]\n"},
	{input: "{ \r\n\t}", output: "{}\n"},
	{input: "(1,2,3,)", output: "(1, 2, 3)\n"},
	{input: "[1,2,3,]", output: "[1, 2, 3]\n"},
	{input: "{1,2,3,}", output: "{1, 2, 3}\n"},
	{input: "{1,2,3,}", output: "{1, 2, 3}\n"},
	{input: "{\n1\n}", output: "{\n    1,\n}\n"},
	{input: "{\n1,\n2\n}", output: "{\n    1,\n    2,\n}\n"},
	{input: "{\nfoo={}   \n}", output: "{\n    foo = {},\n}\n"},
	{input: "foo \n\n\n bar \n\n\n baz \n\n\n", output: "foo\n\nbar\n\nbaz\n"},
	{input: "foo : - 1", output: "foo: -1\n"},
	{input: "foo : - 1.23e45", output: "foo: -1.23e45\n"},
	{input: "foo( rights:A+B )", output: "foo(rights: A + B)\n"},
	{input: "foo( rights:A-B )", output: "foo(rights: A - B)\n"},
	{input: "// comment", output: "// comment\n"},
	{input: " // comment", output: "// comment\n"},
	{input: "foo// comment", output: "foo // comment\n"},
	{input: "foo  // comment", output: "foo // comment\n"},

	{
		input: `
// multiple
  // line
  // comment


  // separate comment
// over here
`,
		output: `
// multiple
// line
// comment

// separate comment
// over here
`,
	},

	{
		input: `
// some comment
    success ( "foo" ){
incorrect = "indentation)]}" ,
and = {
some,    // another comment
more
}
}
`,
		output: `
// some comment
success("foo") {
    incorrect = "indentation)]}",
    and = {
        some, // another comment
        more,
    },
}
`,
	},

	{
		input: `
 decode_failure ( "TooManyHandles" )   {
 // TODO: Close handles on encode/decode failure.
bindings_denylist=[ go,rust ,],
	handle_defs ={
		# 0=event( ),
		# 1 = event ( ) ,
        },
  type=SingleHandle,
bytes ={
  v1 ,v2= [
repeat( 0xff): 4 , padding : 4,
  ]
  },
	  handles = {
  v1  ,  v2  =  [
#0,#1,
  ]
    },
 err = EXTRA_HANDLES,
}

`,
		output: `
decode_failure("TooManyHandles") {
    // TODO: Close handles on encode/decode failure.
    bindings_denylist = [go, rust],
    handle_defs = {
        #0 = event(),
        #1 = event(),
    },
    type = SingleHandle,
    bytes = {
        v1, v2 = [
            repeat(0xff):4, padding:4,
        ],
    },
    handles = {
        v1, v2 = [
            #0, #1,
        ],
    },
    err = EXTRA_HANDLES,
}
`,
	},

	{
		input: `
 decode_failure ( "TooManyHandles" )   {
 // TODO: Close handles on encode/decode failure.
bindings_denylist=[ go,rust ,],
	handle_defs ={
		# 0=event( ),
		# 1 = event ( ) ,
        },
    // gidl-format off
    type=SingleHandle,
        bytes ={
                 v1 ,v2= [
                 repeat( 0xff): 4 , padding : 4,
  // gidl-format on
  ]
  },
}

`,
		output: `
decode_failure("TooManyHandles") {
    // TODO: Close handles on encode/decode failure.
    bindings_denylist = [go, rust],
    handle_defs = {
        #0 = event(),
        #1 = event(),
    },
    // gidl-format off
    type=SingleHandle,
        bytes ={
                 v1 ,v2= [
                 repeat( 0xff): 4 , padding : 4,
  // gidl-format on
        ],
    },
}
`,
	},

	{
		input: `
encode_success("EncodeHandleTypeAndRightsComeFromFIDL") {
	bindings_allowlist = [llcpp, hlcpp, go, rust, dart,],
handle_defs = {
		# 0 = channel ( rights : channel_default ) ,
	},
	value = EventWithDefaultRights {h: #0},
	bytes = {
		v1, v2 = [
		repeat( 0xff) :4, padding: 4
		],
	},
	handle_dispositions = {
		v1,v2 = [
			{#0, type:event, rights:event_default }
		]
	}
}
`,
		output: `
encode_success("EncodeHandleTypeAndRightsComeFromFIDL") {
    bindings_allowlist = [llcpp, hlcpp, go, rust, dart],
    handle_defs = {
        #0 = channel(rights: channel_default),
    },
    value = EventWithDefaultRights{h: #0},
    bytes = {
        v1, v2 = [
            repeat(0xff):4, padding:4,
        ],
    },
    handle_dispositions = {
        v1, v2 = [
            {#0, type: event, rights: event_default},
        ],
    },
}
`,
	},
}

func TestFormat(t *testing.T) {
	for _, tc := range cases {
		expectFormat(t, tc.input, tc.output)
	}
}

func TestFormatIdempotent(t *testing.T) {
	for _, tc := range cases {
		expectFormat(t, tc.output, tc.output)
	}
}

func TestFormatDisabled(t *testing.T) {
	for _, tc := range cases {
		// These directives don't nest.
		if strings.Contains(tc.input, enableComment) {
			continue
		}
		input := fmt.Sprintf("%s\n%s", disableComment, tc.input)
		expectFormat(t, input, input)
	}
}

func expectFormat(t *testing.T, input, output string) {
	t.Helper()
	var b strings.Builder
	if err := format(&b, strings.NewReader(input), "<input>"); err != nil {
		t.Errorf("unexpected error: %s\n\nfor input:\n%s", err, input)
	} else if diff := cmp.Diff(output, b.String()); diff != "" {
		t.Errorf("unexpected output (-want +got):\n%s\n\nfor input:\n%s", diff, input)
	}
}

func TestFormatFails(t *testing.T) {
	cases := []struct {
		input, errSubstring string
	}{
		// Errors from text/scanner (without the specific message since it might
		// change in later versions of Go):
		{`"`, "<input>:1:1:"},     // unterminated string
		{"'foo'", "<input>:1:1:"}, // invalid character literal
		{"\xc3\x28", "<input>"},   // invalid UTF-8 (doesn't give a position)
		// Errors from this package:
		{")", "<input>:1:1: extraenous closing bracket ')'"},
		{"]", "<input>:1:1: extraenous closing bracket ']'"},
		{"}", "<input>:1:1: extraenous closing bracket '}'"},
		{"(]", "<input>:1:2: mismatched closing bracket ']' (expected ')')"},
	}
	for _, tc := range cases {
		var b strings.Builder
		if err := format(&b, strings.NewReader(tc.input), "<input>"); err == nil {
			t.Errorf("unexpected success for input:\n%s", tc.input)
		} else if !strings.Contains(err.Error(), tc.errSubstring) {
			t.Errorf("error %q did not contain %q, for input:\n%s", err, tc.errSubstring, tc.input)
		}
	}
}
