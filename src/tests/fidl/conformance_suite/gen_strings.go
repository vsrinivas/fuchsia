// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"encoding/hex"
	"fmt"
	"text/template"
	"unicode/utf8"
)

type validCase struct {
	value   string
	comment string
}

type invalidCase struct {
	value   string
	comment string
}

type exclude struct {
	testCase                  interface{}
	overrideBindingsAllowlist string
}

var cases = []interface{}{
	// All the following test cases check boundary conditions of code units

	validCase{"\x00", "single byte, min: 0"},
	validCase{"\x7f", "single byte, max: 127"},
	validCase{"\xc2\x80", "two bytes,   min: 128"},
	validCase{"\xdf\xbf", "two bytes,   max: 2047"},
	validCase{"\xe1\x80\x80", "three bytes, min: 2048"},
	validCase{"\xef\xbf\xbf", "three bytes, max: 65535"},
	validCase{"\xf0\x90\x80\x80", "four bytes,  min: 65536"},
	validCase{"\xf4\x8f\xbf\xbf", "four bytes,  max: 1114111"},

	invalidCase{"\x80", "1 above max single byte"},
	invalidCase{"\xc2\x7f", "1 below min two bytes"},
	invalidCase{"\xdf\xc0", "1 above max two bytes"},
	invalidCase{"\xe1\x80\x7f", "1 below min three bytes"},
	invalidCase{"\xef\xbf\xc0", "1 above max three bytes"},
	invalidCase{"\xf0\x80\x80\x80", "1 below min four bytes"},
	invalidCase{"\xf7\xbf\xbf\xc0", "1 above max four bytes"},

	// Invalid continuations for two, three, and four bytes.
	//
	// - 1 test for the first following byte of an initial two byte value not
	//   having the high bit set.
	// - 2 tests for the first and second following byte of an initial three
	//   byte value not having the high bit set.
	// - 3 tests for the first, second, and third following byte of an initial
	//   four byte value not having the high bit set.

	validCase{"\xc2\x80", ""},
	invalidCase{"\xc2\x7f", "first byte following two byte value not starting with 0b10"},

	invalidCase{"\xe1\x7f\x80", "first byte following three byte value not starting with 0b10"},
	invalidCase{"\xe1\x80\x7f", "second byte following three byte value not starting with 0b10"},

	validCase{"\xf0\x90\x80\x80", ""},
	invalidCase{"\xf0\x7f\x80\x80", "first byte following four byte value not starting with 0b10"},
	invalidCase{"\xf0\x90\x7f\x80", "second byte following four byte value not starting with 0b10"},
	invalidCase{"\xf0\x90\x80\x7f", "third byte following four byte value not starting with 0b10"},

	// All encodings of slash, only the shortest is valid.
	//
	// For further details, see "code unit" defined to be 'The minimal bit
	// combination that can represent a unit of encoded text for processing or
	// interchange.'

	validCase{"\x2f", "ascii slash"},
	invalidCase{"\xc0\xaf", "slash (2)"},
	invalidCase{"\xe0\x80\xaf", "slash (3)"},
	invalidCase{"\xf0\x80\x80\xaf", "slash (4)"},

	// All the following test cases are valid non-character code points

	validCase{"\xd8\x9d", "U+061D"},
	validCase{"\xd7\xb6", "U+05F6"},
	validCase{"\xe0\xab\xb4", "U+0AF4"},
	validCase{"\xe0\xb1\x92", "U+0C52"},
	validCase{"\xf0\x9e\x91\x94", "U+1E454"},
	validCase{"\xf0\x9f\xa5\xb8", "U+1F978"},

	// All the following test cases are various miscelleneous strings

	validCase{"", "empty string"},
	validCase{"a", "simply the letter a"},
	validCase{"€", `euro sign, i.e \xe2\x82\xac`},

	validCase{"\x00\xf4\x8f\xbf\xbf\x7f\xf0\x90\x80\x80\xc2\x80", "mix and match from earlier cases"},
	validCase{"\xdf\xbf\xef\xbf\xbf\xe1\x80\x80", "mix and match from earlier cases"},

	// UTF-8 BOM
	// TODO(fxbug.dev/52104): Dart consumes the UTF-8 BOM
	exclude{validCase{"\xef\xbb\xbf", "UTF-8 BOM"}, "[go,rust,]"},
	invalidCase{"\xef", "Partial UTF-8 BOM (1)"},
	invalidCase{"\xef\xbb", "Partial UTF-8 BOM (2)"},

	invalidCase{"\xdf\x80\x80", "invalid partial sequence"},
	invalidCase{"\xe0\x80\x80", "long U+0000, non shortest form"},
	validCase{"\xe1\x80\x80", ""},
	invalidCase{"\xc3\x28", "invalid 2 octet sequence"},

	// All the following test cases are taken from Chromium's
	// streaming_utf8_validator_unittest.cc
	//
	// Some are duplicative to other tests, and have been kept to ease
	// comparison and translation of the tests.

	validCase{"\r", ""},
	validCase{"\n", ""},
	validCase{"a", ""},
	validCase{"\xc2\x81", ""},
	validCase{"\xe1\x80\xbf", ""},
	validCase{"\xf1\x80\xa0\xbf", ""},
	// TODO(fxbug.dev/52104): Dart consumes the UTF-8 BOM
	exclude{validCase{"\xef\xbb\xbf", "UTF-8 BOM"}, "[go,rust,]"},

	// always invalid bytes
	invalidCase{"\xc0", ""},
	invalidCase{"\xc1", ""},
	invalidCase{"\xf5", ""},
	invalidCase{"\xf6", ""},
	invalidCase{"\xf7", ""},
	invalidCase{"\xf8", ""},
	invalidCase{"\xf9", ""},
	invalidCase{"\xfa", ""},
	invalidCase{"\xfb", ""},
	invalidCase{"\xfc", ""},
	invalidCase{"\xfd", ""},
	invalidCase{"\xfe", ""},
	invalidCase{"\xff", ""},

	// surrogate code points
	invalidCase{"\xed\xa0\x80", "U+D800, high surrogate, first"},
	invalidCase{"\xed\xb0\x80", "low surrogate, first"},
	invalidCase{"\xed\xbf\xbf", "low surrogate, last"},

	// overlong sequences
	invalidCase{"\xc0\x80", "U+0000"},
	invalidCase{"\xc1\x80", "\"A\""},
	invalidCase{"\xc1\x81", "\"B\""},
	invalidCase{"\xe0\x80\x80", "U+0000"},
	invalidCase{"\xe0\x82\x80", "U+0080"},
	invalidCase{"\xe0\x9f\xbf", "U+07ff"},
	invalidCase{"\xf0\x80\x80\x8D", "U+000D"},
	invalidCase{"\xf0\x80\x82\x91", "U+0091"},
	invalidCase{"\xf0\x80\xa0\x80", "U+0800"},
	invalidCase{"\xf0\x8f\xbb\xbf", "U+FEFF (BOM)"},
	invalidCase{"\xf8\x80\x80\x80\xbf", "U+003F"},
	invalidCase{"\xfc\x80\x80\x80\xa0\xa5", ""},

	// Beyond U+10FFFF
	invalidCase{"\xf4\x90\x80\x80", "U+110000"},
	invalidCase{"\xf8\xa0\xbf\x80\xbf", "5 bytes"},
	invalidCase{"\xfc\x9c\xbf\x80\xbf\x80", "6 bytes"},

	// BOMs in UTF-16(BE|LE)
	invalidCase{"\xfe\xff", "BOMs in UTF-16 BE"},
	invalidCase{"\xff\xfe", "BOMs in UTF-16 LE"},
}

// TODO(fxbug.dev/52104): UTF8 encoding and decoding is not conformant in Dart.

var successTmpl = template.Must(template.New("tmpls").Parse(
	`{{ if .comment }}// {{ .comment }}{{ end }}
success("StringsValidCase{{ .index }}") {
{{- if .overrideBindingsAllowlist }}
    bindings_allowlist = {{ .overrideBindingsAllowlist }},
{{- else }}
    bindings_allowlist = [go,rust,hlcpp,llcpp,dart,],
{{- end }}
    value = StringWrapper {
        str: "{{ .escapedValue }}",
    },
    bytes = {
        v1 = [
            // length, present
            num({{ .lenValue }}):8,
            repeat(0xff):8,

            // content
{{- range .bytesValue }}
            {{ . }},
{{- end }}
{{- if .padding }}
            padding:{{ .padding }},
{{- end }}
        ],
    },
}
`))

// In Rust, we cannot represent non-UTF8 strings in domain objects since
// std::string::String validates on construction. We therefore omit Rust
// from the 'encode_failure' cases since this could not occur.
//
// See https://doc.rust-lang.org/std/string/struct.String.html#utf-8

var decodeFailureTmpl = template.Must(template.New("tmpls").Parse(
	`{{ if .comment }}// {{ .comment }}{{ end }}
encode_failure("StringsInvalidCase{{ .index }}") {
    bindings_allowlist = [go,hlcpp,llcpp,],
    value = StringWrapper {
        str: "{{ .escapedValue }}",
    },
    err = STRING_NOT_UTF8,
}

{{ if .comment }}// {{ .comment }}{{ end }}
decode_failure("StringsInvalidCase{{ .index }}") {
    bindings_allowlist = [go,rust,hlcpp,llcpp,],
    type = StringWrapper,
    bytes = {
        v1 = [
            // length, present
            num({{ .lenValue }}):8,
            repeat(0xff):8,

            // content
{{- range .bytesValue }}
            {{ . }},
{{- end }}
{{- if .padding }}
            padding:{{ .padding }},
{{- end }}
        ],
    },
    err = STRING_NOT_UTF8,
}
`))

func escapeStr(value string) string {
	var (
		buf    bytes.Buffer
		src    = []byte(value)
		dstLen = hex.EncodedLen(len(src))
		dst    = make([]byte, dstLen)
	)
	hex.Encode(dst, src)
	for i := 0; i < dstLen; i += 2 {
		buf.WriteString("\\x")
		buf.WriteByte(dst[i])
		buf.WriteByte(dst[i+1])
	}
	return buf.String()
}

func fidlAlign(len int) int {
	return (len + 7) & ^7
}

func toTmplParams(index int, value, comment, overrideBindingsAllowlist string) map[string]interface{} {
	return map[string]interface{}{
		"index":                     index,
		"comment":                   comment,
		"escapedValue":              escapeStr(value),
		"lenValue":                  len(value),
		"bytesValue":                []byte(value),
		"padding":                   fidlAlign(len(value)) - len(value),
		"overrideBindingsAllowlist": overrideBindingsAllowlist,
	}
}

type printer struct {
	validIndex, invalidIndex int
}

func (p *printer) print(testCase interface{}, overrideBindingsAllowlist string) {
	switch testCase := testCase.(type) {
	case validCase:
		if !utf8.ValidString(testCase.value) {
			panic(fmt.Sprintf("supposedly valid example seems invalid: %+v", testCase))
		}
		p.validIndex++
		p.printValidCase(toTmplParams(p.validIndex, testCase.value, testCase.comment, overrideBindingsAllowlist))
	case invalidCase:
		if utf8.ValidString(testCase.value) {
			panic(fmt.Sprintf("supposedly invalid example seems valid: %+v", testCase))
		}
		p.invalidIndex++
		p.printInvalidCase(toTmplParams(p.invalidIndex, testCase.value, testCase.comment, overrideBindingsAllowlist))
	case exclude:
		p.print(testCase.testCase, testCase.overrideBindingsAllowlist)
	}
}

func (p *printer) printValidCase(params map[string]interface{}) {
	var buf bytes.Buffer
	if err := successTmpl.Execute(&buf, params); err != nil {
		panic(err)
	}
	fmt.Println(buf.String())
}

func (p *printer) printInvalidCase(params map[string]interface{}) {
	var buf bytes.Buffer
	if err := decodeFailureTmpl.Execute(&buf, params); err != nil {
		panic(err)
	}
	fmt.Println(buf.String())
}

func main() {
	fmt.Printf(`// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// DO NOT EDIT; Cases below are generated with:
//
//     go run src/tests/fidl/conformance_suite/gen_strings.go > src/tests/fidl/conformance_suite/strings_utf8.gen.gidl
//

`)
	var p printer
	for _, testCase := range cases {
		p.print(testCase, "")
	}
}
