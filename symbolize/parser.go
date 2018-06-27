// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"strconv"
)

// TODO: Implement a reflection based means of automatically doing these conversions.
func str2dec(what string) uint64 {
	out, err := strconv.ParseUint(what, 10, 64)
	if err != nil {
		panic(err.Error())
	}
	return out
}

func str2int(what string) uint64 {
	out, err := strconv.ParseUint(what, 0, 64)
	if err != nil {
		panic(err.Error())
	}
	return out
}

func str2float(what string) float64 {
	out, err := strconv.ParseFloat(what, 64)
	if err != nil {
		panic(err.Error())
	}
	return out
}

const (
	decRegexp   = "(?:[[:digit:]]+)"
	ptrRegexp   = "(?:0|0x[[:xdigit:]]{1,16})"
	strRegexp   = "(?:[^{}:]*)"
	spaceRegexp = `(?:\s*)`
	floatRegexp = `(?:[[:digit:]]+)\.(?:[[:digit:]]+)`
)

type ParseLineFunc func(msg string) []Node

func GetLineParser() ParseLineFunc {
	var b regexpTokenizerBuilder
	out := []Node{}
	dec := decRegexp
	ptr := ptrRegexp
	str := strRegexp
	num := fmt.Sprintf("(?:%s|%s)", dec, ptr)
	b.addRule(fmt.Sprintf("{{{bt:(%s):(%s)}}}", dec, ptr), func(args ...string) {
		out = append(out, &BacktraceElement{
			num:   str2dec(args[1]),
			vaddr: str2int(args[2]),
		})
	})
	b.addRule(fmt.Sprintf("{{{pc:(%s)}}}", ptr), func(args ...string) {
		out = append(out, &PCElement{vaddr: str2int(args[1])})
	})
	b.addRule(fmt.Sprintf("\033\\[(%s)m", dec), func(args ...string) {
		out = append(out, &ColorCode{color: str2dec(args[1])})
	})
	b.addRule(fmt.Sprintf("{{{dumpfile:(%s):(%s)}}}", str, str), func(args ...string) {
		out = append(out, &DumpfileElement{sinkType: args[1], name: args[2]})
	})
	b.addRule(fmt.Sprintf(`{{{module:(%s):(%s):elf:(%s)}}}`, num, str, str), func(args ...string) {
		out = append(out, &ModuleElement{mod: Module{
			Id:    str2int(args[1]),
			Name:  args[2],
			Build: args[3],
		}})
	})
	b.addRule(fmt.Sprintf(`{{{mmap:(%s):(%s):load:(%s):(%s):(%s)}}}`, ptr, num, num, str, ptr), func(args ...string) {
		out = append(out, &MappingElement{seg: Segment{
			Vaddr:      str2int(args[1]),
			Size:       str2int(args[2]),
			Mod:        str2int(args[3]),
			Flags:      args[4],
			ModRelAddr: str2int(args[5]),
		}})
	})
	b.addRule(`{{{reset}}}`, func(args ...string) {
		out = append(out, &ResetElement{})
	})
	tokenizer, err := b.compile(func(text string) {
		out = append(out, &Text{text: text})
	})
	if err != nil {
		panic(err.Error())
	}
	return func(msg string) []Node {
		out = nil
		tokenizer.run(msg)
		return out
	}
}

func StartParsing(ctx context.Context, reader io.Reader) <-chan InputLine {
	out := make(chan InputLine)
	// This is not used for demuxing. It is a human readable line number.
	var lineno uint64 = 1
	var b regexpTokenizerBuilder
	space := spaceRegexp
	float := floatRegexp
	dec := decRegexp
	tags := `[^\[\]]*`
	b.addRule(fmt.Sprintf(`\[(%s)\]%s(%s)\.(%s)>%s(.*)$`, float, space, dec, dec, space), func(args ...string) {
		var hdr logHeader
		var line InputLine
		hdr.time = str2float(args[1])
		hdr.process = str2dec(args[2])
		hdr.thread = str2dec(args[3])
		line.header = hdr
		line.source = Process(hdr.process)
		line.lineno = lineno
		line.msg = args[4]
		out <- line
	})
	b.addRule(fmt.Sprintf(`\[(%s)\]\[(%s)\]\[(%s)\]\[(%s)\]%s(.*)$`, float, dec, dec, tags, space), func(args ...string) {
		var hdr sysLogHeader
		var line InputLine
		hdr.time = str2float(args[1])
		hdr.process = str2dec(args[2])
		hdr.thread = str2dec(args[3])
		hdr.tags = args[4]
		line.header = hdr
		line.source = Process(hdr.process)
		line.lineno = lineno
		line.msg = args[5]
		out <- line
	})
	tokenizer, err := b.compile(func(text string) {
		var line InputLine
		line.source = DummySource{}
		line.msg = text
		line.lineno = lineno
		out <- line
	})
	if err != nil {
		panic(err.Error())
	}
	go func() {
		defer close(out)
		scanner := bufio.NewScanner(reader)
		for ; scanner.Scan(); lineno++ {
			select {
			case <-ctx.Done():
				return
			default:
				tokenizer.run(scanner.Text())
			}
		}
	}()
	return out
}
