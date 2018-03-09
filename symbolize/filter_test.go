// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"fmt"
	"testing"
)

type mockModule struct {
	name      string
	addr2line map[uint64][]SourceLocation
}

type mockSymbolizer struct {
	modules map[string]mockModule
}

func newMockSymbolizer(modules []mockModule) Symbolizer {
	var out mockSymbolizer
	out.modules = make(map[string]mockModule)
	for _, mod := range modules {
		out.modules[mod.name] = mod
	}
	return &out
}

func (s *mockSymbolizer) FindSrcLoc(file string, modRelAddr uint64) <-chan LLVMSymbolizeResult {
	out := make(chan LLVMSymbolizeResult, 1)
	if mod, ok := s.modules[file]; ok {
		if locs, ok := mod.addr2line[modRelAddr]; ok {
			out <- LLVMSymbolizeResult{locs, nil}
		} else {
			out <- LLVMSymbolizeResult{nil, fmt.Errorf("0x%x was not a valid address in %s", modRelAddr, file)}
		}
	} else {
		out <- LLVMSymbolizeResult{nil, fmt.Errorf("%s could not be found", file)}
	}
	return out
}

type symbolizerRepo struct {
	builds map[string]string
}

func ExampleBasic() {
	// mock the input and outputs of llvm-symbolizer
	symbo := newMockSymbolizer([]mockModule{
		{"out/libc.so", map[uint64][]SourceLocation{
			0x429c0: {{"atan2.c", 49, "atan2"}, {"math.h", 51, "__DOUBLE_FLOAT"}},
			0x43680: {{"pow.c", 23, "pow"}},
			0x44987: {{"memcpy.c", 76, "memcpy"}},
		}},
		{"out/libcrypto.so", map[uint64][]SourceLocation{
			0x81000: {{"rsa.c", 101, "mod_exp"}},
			0x82000: {{"aes.c", 17, "gf256_mul"}},
			0x83000: {{"aes.c", 560, "gf256_div"}},
		}},
	})
	// mock ids.txt
	repo := NewRepo()
	repo.AddObjects(map[string]string{
		"be4c4336e20b734db97a58e0f083d0644461317c": "out/libc.so",
		"b4b6c520ccf0aa11ff71d8ded7d6a2bc03037aa1": "out/libcrypto.so",
	})

	// make an actual filter using those two mock objects
	filter := NewFilter(repo, symbo)

	// parse some example lines
	filter.AddModule(Module{"libc.so", "be4c4336e20b734db97a58e0f083d0644461317c", 1})
	filter.AddModule(Module{"libcrypto.so", "b4b6c520ccf0aa11ff71d8ded7d6a2bc03037aa1", 2})
	filter.AddSegment(Segment{1, 0x12345000, 849596, "rx", 0x0})
	filter.AddSegment(Segment{2, 0x23456000, 539776, "rx", 0x80000})
	line := ParseLine("\033[1m Error at {{{pc:0x123879c0}}}")
	// print out a more precise form
	line.Accept(&FilterVisitor{filter})
	jsonify := &JsonVisitor{}
	line.Accept(jsonify)
	json, err := jsonify.GetJson()
	if err != nil {
		fmt.Printf("json did not parse correctly: %v", err)
		return
	}
	fmt.Print(string(json))

	// Output:
	//{
	//	"type": "group",
	//	"children": [
	//		{
	//			"type": "color",
	//			"color": 1,
	//			"children": [
	//				{
	//					"type": "text",
	//					"text": " Error at "
	//				},
	//				{
	//					"type": "pc",
	//					"vaddr": 305691072,
	//					"file": "atan2.c",
	//					"line": 49,
	//					"function": "atan2"
	//				}
	//			]
	//		}
	//	]
	//}
}

func TestMalformed(t *testing.T) {
	// Parse a bad line
	line := ParseLine("\033[1m Error at {{{pc:0x123879c0")

	if line != nil {
		t.Error("expected", nil, "got", line)
	}
}
