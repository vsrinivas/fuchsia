// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"bytes"
	"context"
	"os"
	"strings"
	"testing"
)

func TestColor(t *testing.T) {
	// TODO(jakehehrlich): Change presenter so that redundent resets are not used when user input already contains them.
	msg := "[0.0] 1234.5678> \033[1mThis is bold \033[31mThis is red and bold \033[37mThis is bold white\n" +
		"[0.0] 1234.5678> This is just normal and has no trailing ANSI code\n" +
		"[0.0] 1234.5678> \033[1m\033[31m this line tests adjacent state changes\n" +
		"[0.0] 1234.5678> \033[1m\033[31m this line will eventully test non-redundent reset \033[1m\033\n"
	symbo := newMockSymbolizer([]mockModule{})
	repo := NewRepo()
	demuxer := NewDemuxer(repo, symbo)
	ctx := context.Background()
	in := StartParsing(ctx, strings.NewReader(msg))
	out := demuxer.Start(ctx, in)
	buf := new(bytes.Buffer)
	presenter := NewBasicPresenter(buf, true)
	presenter.Start(out)
	expected := "[0.000] 1234.5678> \033[1mThis is bold \033[31mThis is red and bold \033[37mThis is bold white\n\033[0m" +
		"[0.000] 1234.5678> This is just normal and has no trailing ANSI code\n" +
		"[0.000] 1234.5678> \033[1m\033[31m this line tests adjacent state changes\n\033[0m" +
		"[0.000] 1234.5678> \033[1m\033[31m this line will eventully test non-redundent reset \033[1m\033\n\033[0m"
	actual := buf.String()
	if actual != expected {
		t.Error("expected", expected, "got", actual)
	}
}

func ExampleDemux() {
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

	// make a demuxer
	demuxer := NewDemuxer(repo, symbo)

	// define a little message that will need to be parsed
	msg := "[131.200] 1234.5678> {{{module:1:libc.so:elf:be4c4336e20b734db97a58e0f083d0644461317c}}}\n" +
		"[131.301] 1234.5678> {{{module:2:libcrypto.so:elf:b4b6c520ccf0aa11ff71d8ded7d6a2bc03037aa1}}}\n" +
		"[131.402] 1234.5678> {{{mmap:0x12345000:849596:load:1:rx:0x0}}}\n" +
		"[131.503] 1234.5678> {{{mmap:0x23456000:539776:load:2:rx:0x80000}}}\n" +
		"[131.604] 1234.5678> \033[1mError at {{{pc:0x123879c0}}}\n"

	// start sending InputLines to the demuxer
	ctx := context.Background()
	in := StartParsing(ctx, strings.NewReader(msg))
	// start the demuxer which will cause filters to send output lines to 'out'
	out := demuxer.Start(ctx, in)

	presenter := NewBasicPresenter(os.Stdout, false)
	presenter.Start(out)

	//Output:
	//[131.200] 1234.5678> {{{module:be4c4336e20b734db97a58e0f083d0644461317c:libc.so:1}}}
	//[131.301] 1234.5678> {{{module:b4b6c520ccf0aa11ff71d8ded7d6a2bc03037aa1:libcrypto.so:2}}}
	//[131.402] 1234.5678> {{{mmap:0x12345000:849596:load:1:rx:0x0}}}
	//[131.503] 1234.5678> {{{mmap:0x23456000:539776:load:2:rx:0x80000}}}
	//[131.604] 1234.5678> Error at atan2.c:49
}
