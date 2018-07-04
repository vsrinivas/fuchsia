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

func TestSyslog(t *testing.T) {
	msg := "[123.456][1234][05678][klog] Blarg\n"
	symbo := newMockSymbolizer([]mockModule{})
	repo := NewRepo()
	demuxer := NewDemuxer(repo, symbo)
	ctx := context.Background()
	in := StartParsing(ctx, strings.NewReader(msg))
	out := demuxer.Start(ctx, in)
	buf := new(bytes.Buffer)
	presenter := NewBasicPresenter(buf, true)
	presenter.Start(out)
	expected := "[00123.456000][1234][5678][klog] Blarg\n"
	actual := buf.String()
	if actual != expected {
		t.Error("expected", expected, "got", actual)
	}
}

func TestColor(t *testing.T) {
	// TODO(jakehehrlich): Change presenter so that redundent resets are not used when user input already contains them.
	msg := "[0.0] 01234.5678> \033[1mThis is bold \033[31mThis is red and bold \033[37mThis is bold white\n" +
		"[0.0] 1234.05678> This is just normal and has no trailing ANSI code\n" +
		"[0.0] 1234.5678> \033[1m\033[31m this line tests adjacent state changes\n" +
		"[0.0] 01234.5678> \033[1m\033[31m this line will eventully test non-redundent reset \033[1m\033\n"
	symbo := newMockSymbolizer([]mockModule{})
	repo := NewRepo()
	demuxer := NewDemuxer(repo, symbo)
	ctx := context.Background()
	in := StartParsing(ctx, strings.NewReader(msg))
	out := demuxer.Start(ctx, in)
	buf := new(bytes.Buffer)
	presenter := NewBasicPresenter(buf, true)
	presenter.Start(out)
	expected := "[0.000] 01234.05678> \033[1mThis is bold \033[31mThis is red and bold \033[37mThis is bold white\n\033[0m" +
		"[0.000] 01234.05678> This is just normal and has no trailing ANSI code\n" +
		"[0.000] 01234.05678> \033[1m\033[31m this line tests adjacent state changes\n\033[0m" +
		"[0.000] 01234.05678> \033[1m\033[31m this line will eventully test non-redundent reset \033[1m\033\n\033[0m"
	actual := buf.String()
	if actual != expected {
		t.Error("expected", expected, "got", actual)
	}
}

func ExampleDummyProcess() {
	// mock the input and outputs of llvm-symbolizer
	symbo := newMockSymbolizer([]mockModule{
		{"testdata/libc.elf", map[uint64][]SourceLocation{
			0x429c0: {{NewOptStr("atan2.c"), 33, NewOptStr("atan2")}},
		}},
	})

	// mock ids.txt
	repo := NewRepo()
	repo.AddSource(testBinaries)

	// make a demuxer
	demuxer := NewDemuxer(repo, symbo)

	// define a little message that will need to be parsed
	msg := "{{{module:1:libc.so:elf:4fcb712aa6387724a9f465a32cd8c14b}}}\n" +
		"{{{mmap:0x12345000:849596:load:1:rx:0x0}}}\n" +
		"{{{pc:0x123879c0}}}\n" +
		"blarg[0.0] 0.0> This should be on it's own line\n"

	// start sending InputLines to the demuxer
	ctx := context.Background()
	in := StartParsing(ctx, strings.NewReader(msg))
	// start the demuxer which will cause filters to send output lines to 'out'
	out := demuxer.Start(ctx, in)

	presenter := NewBasicPresenter(os.Stdout, false)
	presenter.Start(out)

	//Output:
	//atan2 at atan2.c:33
	//blarg
	//[0.000] 00000.00000> This should be on it's own line
}

func ExampleDemux() {
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

	// mock ids.txt:q
	repo := NewRepo()
	repo.AddSource(testBinaries)

	// make a demuxer
	demuxer := NewDemuxer(repo, symbo)

	// define a little message that will need to be parsed
	msg := "[131.200] 1234.5678> keep {{{module:1:libc.so:elf:4fcb712aa6387724a9f465a32cd8c14b}}}\n" +
		"[131.301] 1234.5678> {{{module:2:libcrypto.so:elf:b4b6c520ccf0aa11ff71d8ded7d6a2bc03037aa1}}} keep\n" +
		"[131.402] 1234.5678> {{{mmap:0x12345000:0xcf6bc:load:1:rx:0x0}}}\n" +
		"[131.503] 1234.5678> {{{mmap:0x23456000:0x83c80:load:2:rx:0x80000}}}\n" +
		"[131.604] 1234.5678> \033[1mError at {{{pc:0x123879c0}}}\n"

	// start sending InputLines to the demuxer
	ctx := context.Background()
	in := StartParsing(ctx, strings.NewReader(msg))
	// start the demuxer which will cause filters to send output lines to 'out'
	out := demuxer.Start(ctx, in)

	presenter := NewBasicPresenter(os.Stdout, false)
	presenter.Start(out)

	//Output:
	//[131.200] 01234.05678> keep {{{module:4fcb712aa6387724a9f465a32cd8c14b:libc.so:1}}}
	//[131.301] 01234.05678> {{{module:b4b6c520ccf0aa11ff71d8ded7d6a2bc03037aa1:libcrypto.so:2}}} keep
	//[131.604] 01234.05678> Error at atan2 at atan2.c:49
}

func ExampleBadAddr() {
	// mock the input and outputs of llvm-symbolizer
	symbo := newMockSymbolizer([]mockModule{
		{"testdata/libc.elf", map[uint64][]SourceLocation{
			0x429c0: {{EmptyOptStr(), 0, NewOptStr("atan2")}, {NewOptStr("math.h"), 51, NewOptStr("__DOUBLE_FLOAT")}},
			0x43680: {{NewOptStr("pow.c"), 67, EmptyOptStr()}},
			0x44987: {{NewOptStr("memcpy.c"), 76, NewOptStr("memcpy")}},
		}},
	})

	// mock ids.txt
	repo := NewRepo()
	repo.AddSource(testBinaries)

	// make a demuxer
	demuxer := NewDemuxer(repo, symbo)

	// define a little message that will need to be parsed
	msg := "[131.200] 1234.5678> {{{module:1:libc.so:elf:4fcb712aa6387724a9f465a32cd8c14b}}}\n" +
		"[131.402] 1234.5678> {{{mmap:0x12345000:0xcf6bc:load:1:rx:0x0}}}\n" +
		"[131.604] 1234.5678> {{{pc:0x123879ff}}}\n" +
		"[131.605] 1234.5678> {{{pc:0x123879c0}}}\n" +
		"[131.606] 1234.5678> {{{pc:0x12388680}}}\n"

	// start sending InputLines to the demuxer
	ctx := context.Background()
	in := StartParsing(ctx, strings.NewReader(msg))
	// start the demuxer which will cause filters to send output lines to 'out'
	out := demuxer.Start(ctx, in)

	presenter := NewBasicPresenter(os.Stdout, false)
	presenter.Start(out)

	//Output:
	//[131.604] 01234.05678> <libc.so>+0x429ff
	//[131.605] 01234.05678> atan2 at <libc.so>+0x429c0
	//[131.606] 01234.05678> pow.c:67
}
