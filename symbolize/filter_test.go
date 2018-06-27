// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"context"
	"encoding/json"
	"fmt"
	"reflect"
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

func (s *mockSymbolizer) FindSrcLoc(file, build string, modRelAddr uint64) <-chan LLVMSymbolizeResult {
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

func TestBasic(t *testing.T) {
	// mock the input and outputs of llvm-symbolizer
	symbo := newMockSymbolizer([]mockModule{
		{"testdata/libc.elf", map[uint64][]SourceLocation{
			0x429c0: {{NewOptStr("atan2.c"), 49, NewOptStr("atan2")}, {NewOptStr("math.h"), 51, NewOptStr("__DOUBLE_FLOAT")}},
			0x43680: {{NewOptStr("pow.c"), 23, NewOptStr("pow")}},
			0x44987: {{NewOptStr("memcpy.c"), 76, NewOptStr("memcpy")}},
		}},
		{"testdata/libcrypto.elf", map[uint64][]SourceLocation{
			0x81000: {{NewOptStr("rsa.c"), 101, NewOptStr("mod_exp")}},
			0x82000: {{NewOptStr("aes.c"), 17, NewOptStr("gf256_mul")}},
			0x83000: {{NewOptStr("aes.c"), 560, NewOptStr("gf256_div")}},
		}},
	})
	// Get a line parser
	parseLine := GetLineParser()

	// mock ids.txt
	repo := NewRepo()
	repo.AddSource(testBinaries)

	// make an actual filter using those two mock objects
	filter := NewFilter(repo, symbo)

	// parse some example lines
	err := filter.addModule(Module{"libc.elf", "4fcb712aa6387724a9f465a32cd8c14b", 1})
	if err != nil {
		t.Fatal(err)
	}
	err = filter.addModule(Module{"libcrypto.elf", "12ef5c50b3ed3599c07c02d4509311be", 2})
	if err != nil {
		t.Fatal(err)
	}
	filter.addSegment(Segment{1, 0x12345000, 849596, "rx", 0x0})
	filter.addSegment(Segment{2, 0x23456000, 539776, "rx", 0x80000})
	line := parseLine("\033[1m Error at {{{pc:0x123879c0}}}")
	// print out a more precise form
	for _, token := range line {
		token.Accept(&filterVisitor{filter, 1, context.Background(), DummySource{}})
	}
	json, err := GetLineJson(line)
	if err != nil {
		t.Fatalf("json did not parse correctly: %v", err)
	}

	expectedJson := []byte(`[
    {"type": "color", "color": 1},
    {"type": "text", "text": " Error at "},
    {"type": "pc", "vaddr": 305691072, "file": "atan2.c",
     "line": 49, "function": "atan2"}
  ]`)

	if !EqualJson(json, expectedJson) {
		t.Error("expected", expectedJson, "got", json)
	}
}

func TestMalformed(t *testing.T) {
	parseLine := GetLineParser()
	// Parse a bad line
	line := parseLine("\033[1m Error at {{{pc:0x123879c0")
	// Malformed lines should still parse
	if line == nil {
		t.Error("expected", "not nil", "got", line)
	}
}

func EqualJson(a, b []byte) bool {
	var j1, j2 interface{}
	err := json.Unmarshal(a, &j1)
	if err != nil {
		panic(err.Error())
	}
	err = json.Unmarshal(b, &j2)
	if err != nil {
		panic(err.Error())
	}
	return reflect.DeepEqual(j1, j2)
}

func TestBacktrace(t *testing.T) {
	parseLine := GetLineParser()
	line := parseLine("Error at {{{bt:0:0x12389987}}}")

	if line == nil {
		t.Error("got", nil, "expected", "not nil")
		return
	}

	// mock the input and outputs of llvm-symbolizer
	symbo := newMockSymbolizer([]mockModule{
		{"testdata/libc.elf", map[uint64][]SourceLocation{
			0x44987: {{NewOptStr("duff.h"), 64, NewOptStr("duffcopy")}, {NewOptStr("memcpy.c"), 76, NewOptStr("memcpy")}},
		}},
	})
	// mock ids.txt
	repo := NewRepo()
	err := repo.AddSource(testBinaries)
	if err != nil {
		t.Fatal(err)
	}

	// make an actual filter using those two mock objects
	filter := NewFilter(repo, symbo)

	// add some context
	err = filter.addModule(Module{"libc.so", "4fcb712aa6387724a9f465a32cd8c14b", 1})
	if err != nil {
		t.Fatal(err)
	}
	filter.addSegment(Segment{1, 0x12345000, 849596, "rx", 0x0})
	for _, token := range line {
		token.Accept(&filterVisitor{filter, 1, context.Background(), DummySource{}})
	}

	json, err := GetLineJson(line)
	if err != nil {
		t.Error("json did not parse correctly", err)
	}

	expectedJson := []byte(`[
    {"type": "text", "text": "Error at "},
    {"type": "bt", "vaddr": 305699207, "num": 0, "locs":[
      {"line": 64, "function": "duffcopy", "file": "duff.h"},
      {"line": 76, "function": "memcpy", "file": "memcpy.c"}
    ]}
  ]`)

	if !EqualJson(json, expectedJson) {
		t.Error("unexpected json output", "got", string(json), "expected", string(expectedJson))
	}
}

func TestReset(t *testing.T) {
	parseLine := GetLineParser()
	line := parseLine("{{{reset}}}")

	json, err := GetLineJson(line)
	if err != nil {
		t.Error("json did not parse correctly", err)
	}

	expectedJson := []byte(`[{"type":"reset"}]`)
	if !EqualJson(json, expectedJson) {
		t.Error("unexpected json output", "got", string(json), "expected", string(expectedJson))
	}

	// mock the input and outputs of llvm-symbolizer
	symbo := newMockSymbolizer([]mockModule{
		{"testdata/libc.elf", map[uint64][]SourceLocation{
			0x44987: {{NewOptStr("memcpy.c"), 76, NewOptStr("memcpy")}},
		}},
	})
	// mock ids.txt
	repo := NewRepo()
	err = repo.AddSource(testBinaries)

	if err != nil {
		t.Fatal(err)
	}

	// make an actual filter using those two mock objects
	filter := NewFilter(repo, symbo)

	// add some context
	mod := Module{"libc.so", "4fcb712aa6387724a9f465a32cd8c14b", 1}
	err = filter.addModule(mod)
	if err != nil {
		t.Fatal(err)
	}
	seg := Segment{1, 0x12345000, 849596, "rx", 0x0}
	filter.addSegment(seg)

	addr := uint64(0x12389987)

	if info, err := filter.findInfoForAddress(addr); err != nil {
		t.Error("expected", nil, "got", err)
		if len(info.locs) != 1 {
			t.Error("expected", 1, "source location but got", len(info.locs))
		} else {
			loc := SourceLocation{NewOptStr("memcpy.c"), 76, NewOptStr("memcpy")}
			if info.locs[0] != loc {
				t.Error("expected", loc, "got", info.locs[0])
			}
		}
		if info.mod != mod {
			t.Error("expected", mod, "got", info.mod)
		}
		if info.seg != seg {
			t.Error("expected", seg, "got", info.seg)
		}
		if info.addr != addr {
			t.Error("expected", addr, "got", info.addr)
		}
	}

	// now forget the context
	for _, token := range line {
		token.Accept(&filterVisitor{filter, 1, context.Background(), DummySource{}})
	}

	if _, err := filter.findInfoForAddress(addr); err == nil {
		t.Error("expected non-nil error but got", err)
	}
}
