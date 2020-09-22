// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"fmt"
	"io"
	"sort"
	"unicode"
)

// BacktracePresenter intercepts backtrace elements on their own line and
// presents them in text. Inlines are output as separate lines.
// A PostProcessor is taken as an input to synchronously compose another
// PostProcessor
type BacktracePresenter struct {
	out  io.Writer
	next PostProcessor
}

// NewBacktracePresenter constructs a BacktracePresenter.
func NewBacktracePresenter(out io.Writer, next PostProcessor) *BacktracePresenter {
	return &BacktracePresenter{
		out:  out,
		next: next,
	}
}

func printBacktrace(out io.Writer, hdr LineHeader, frame uint64, msg string, info addressInfo) {
	modRelAddr := info.addr - info.seg.Vaddr + info.seg.ModRelAddr
	var hdrString string
	if hdr != nil {
		hdrString = hdr.Present()
	}
	if len(info.locs) == 0 {
		fmt.Fprintf(out, "%s    #%-4d %#016x in <%s>+%#x %s\n", hdrString, frame, info.addr, info.mod.Name, modRelAddr, msg)
		return
	}
	for i, loc := range info.locs {
		i = len(info.locs) - i - 1
		fmt.Fprintf(out, "%s    ", hdrString)
		var frameStr string
		if i == 0 {
			frameStr = fmt.Sprintf("#%d", frame)
		} else {
			frameStr = fmt.Sprintf("#%d.%d", frame, i)
		}
		fmt.Fprintf(out, "%-5s", frameStr)
		fmt.Fprintf(out, " %#016x", info.addr)
		if !loc.function.IsEmpty() {
			fmt.Fprintf(out, " in %v", loc.function)
		}
		if !loc.file.IsEmpty() {
			fmt.Fprintf(out, " %s:%d", loc.file, loc.line)
		}
		fmt.Fprintf(out, " <%s>+%#x", info.mod.Name, modRelAddr)
		if msg != "" {
			fmt.Fprintf(out, " %s", msg)
		}
		fmt.Fprintf(out, "\n")
	}
}

func isSpace(s string) bool {
	for _, r := range s {
		if !unicode.IsSpace(r) {
			return false
		}
	}
	return true
}

func (b *BacktracePresenter) Process(line OutputLine, out chan<- OutputLine) {
	if len(line.line) == 1 {
		if bt, ok := line.line[0].(*BacktraceElement); ok {
			printBacktrace(b.out, line.header, bt.num, bt.msg, bt.info)
			// Don't process a backtrace we've already output.
			return
		}
	}
	if len(line.line) == 2 {
		// Note that we're going to discard the text in front.
		if txt, ok := line.line[0].(*Text); ok && isSpace(txt.text) {
			if bt, ok := line.line[1].(*BacktraceElement); ok {
				printBacktrace(b.out, line.header, bt.num, bt.msg, bt.info)
				// Don't process a backtrace we've already output.
				return
			}
		}
	}
	b.next.Process(line, out)
}

func printLine(line LogLine, fmtStr string, args ...interface{}) OutputLine {
	node := Text{text: fmt.Sprintf(fmtStr, args...)}
	return OutputLine{LogLine: line, line: []Node{&node}}
}

// Because apparently this is the world we live in, I have to write my own
// min/max function.
func min(x, y uint64) uint64 {
	if x < y {
		return x
	}
	return y
}

type dsoInfo struct {
	id    uint64
	name  string
	build string
	addr  *uint64
}

type ContextPresenter map[LineSource]map[uint64]dsoInfo

func (c ContextPresenter) Process(line OutputLine, out chan<- OutputLine) {
	if _, ok := c[line.source]; !ok {
		c[line.source] = make(map[uint64]dsoInfo)
	}
	info := c[line.source]
	blank := true
	skip := false
	for _, token := range line.line {
		switch t := token.(type) {
		case *ResetElement:
			skip = true
			delete(c, line.source)
			break
		case *ModuleElement:
			skip = true
			if _, ok := info[t.mod.Id]; !ok {
				info[t.mod.Id] = dsoInfo{id: t.mod.Id, name: t.mod.Name, build: t.mod.Build}
			}
			break
		case *MappingElement:
			skip = true
			dInfo, ok := info[t.seg.Mod]
			if !ok {
				// We might be missing the module because a non-context element was interleaved.
				// It could also be missing but that isn't this function's job to point out.
				out <- printLine(line.LogLine, " [[[ELF seg #%#x %#x]]]", t.seg.Mod, t.seg.Vaddr-t.seg.ModRelAddr)
				break
			}
			if dInfo.addr == nil {
				dInfo.addr = &t.seg.Vaddr
			} else {
				newAddr := min(*dInfo.addr, t.seg.Vaddr-t.seg.ModRelAddr)
				dInfo.addr = &newAddr
			}
			info[t.seg.Mod] = dInfo
			break
		default:
			// Save this token for output later
			if t, ok := token.(*Text); !ok || !isSpace(t.text) {
				blank = false
			}
		}
	}
	if !skip || !blank {
		// Output all contextual information we've thus far consumed.
		sortedInfo := []dsoInfo{}
		for _, dInfo := range info {
			sortedInfo = append(sortedInfo, dInfo)
		}
		sort.Slice(sortedInfo, func(i, j int) bool {
			return sortedInfo[i].id < sortedInfo[j].id
		})
		// TODO(fxbug.dev/27338): We'd really like something more like the following:
		// [[[ELF module #0 "libc.so" BuildID=1234abcdef 0x12345000(r)-0x12356000(rx)-0x12378000(rw)-0x12389000]]]
		// but this requires a fair bit more work to track.
		for _, dInfo := range sortedInfo {
			if dInfo.addr != nil {
				out <- printLine(line.LogLine, " [[[ELF module #%#x \"%s\" BuildID=%s %#x]]]", dInfo.id, dInfo.name, dInfo.build, *dInfo.addr)
			} else {
				out <- printLine(line.LogLine, " [[[ELF module #%#x \"%s\" BuildID=%s]]]", dInfo.id, dInfo.name, dInfo.build)
			}
		}
		// Now so that we don't print this information out again, forget it all.
		delete(c, line.source)
		out <- line
	}
}

// OptimizeColor attempts to transform output elements to use as few color
// transisitions as is possible
type OptimizeColor struct {
}

func (o *OptimizeColor) Process(line OutputLine, out chan<- OutputLine) {
	// Maintain a current simulated color state
	curColor := uint64(0)
	curBold := false
	// Keep track of the color state at the end of 'out'
	color := uint64(0)
	bold := false
	// The new list of tokens we will output
	newLine := []Node{}
	// Go though each token
	for _, token := range line.line {
		if colorCode, ok := token.(*ColorCode); ok {
			// If we encounter a color update the simulated color state
			if colorCode.color == 1 {
				curBold = true
			} else if colorCode.color == 0 {
				curColor = 0
				curBold = false
			} else {
				curColor = colorCode.color
			}
		} else {
			// If we encounter a non-color token make sure we output
			// colors to handle the transition from the last color to the
			// new color.
			if curColor == 0 && color != 0 {
				color = 0
				bold = false
				newLine = append(newLine, &ColorCode{color: 0})
			} else if curColor != color {
				color = curColor
				newLine = append(newLine, &ColorCode{color: curColor})
			}
			// Make sure to bold the output even if a color 0 code was just output
			if curBold && !bold {
				bold = true
				newLine = append(newLine, &ColorCode{color: 1})
			}
			// Append all non-color nodes
			newLine = append(newLine, token)
		}
	}
	// If the color state isn't already clear, clear it
	if color != 0 || bold != false {
		newLine = append(newLine, &ColorCode{color: 0})
	}
	line.line = newLine
	out <- line
}

// BasicPresenter is a presenter to output very basic uncolored output
type BasicPresenter struct {
	enableColor bool
	output      io.Writer
}

func NewBasicPresenter(output io.Writer, enableColor bool) *BasicPresenter {
	return &BasicPresenter{output: output, enableColor: enableColor}
}

func (b *BasicPresenter) printSrcLoc(loc SourceLocation, info addressInfo) {
	modRelAddr := info.addr - info.seg.Vaddr + info.seg.ModRelAddr
	if !loc.function.IsEmpty() {
		fmt.Fprintf(b.output, "%s at ", loc.function)
	}
	if !loc.file.IsEmpty() {
		fmt.Fprintf(b.output, "%s:%d", loc.file, loc.line)
	} else {
		fmt.Fprintf(b.output, "<%s>+0x%x", info.mod.Name, modRelAddr)
	}
}

func (b *BasicPresenter) Process(res OutputLine, out chan<- OutputLine) {
	if res.header != nil {
		fmt.Fprintf(b.output, "%s", res.header.Present())
	}
	for _, token := range res.line {
		switch node := token.(type) {
		case *BacktraceElement:
			if len(node.info.locs) == 0 {
				b.printSrcLoc(SourceLocation{}, node.info)
			}
			for i, loc := range node.info.locs {
				b.printSrcLoc(loc, node.info)
				if i != len(node.info.locs)-1 {
					fmt.Fprintf(b.output, " inlined from ")
				}
			}
		case *PCElement:
			if len(node.info.locs) > 0 {
				b.printSrcLoc(node.info.locs[0], node.info)
			} else {
				b.printSrcLoc(SourceLocation{}, node.info)
			}
		case *ColorCode:
			if b.enableColor {
				fmt.Fprintf(b.output, "\033[%dm", node.color)
			}
		case *Text:
			fmt.Fprintf(b.output, "%s", node.text)
		case *DumpfileElement:
			fmt.Fprintf(b.output, "{{{dumpfile:%s:%s}}}", node.sinkType, node.name)
		case *ResetElement:
			fmt.Fprintf(b.output, "{{{reset}}}")
		case *ModuleElement:
			fmt.Fprintf(b.output, "{{{module:%s:%s:%d}}}", node.mod.Build, node.mod.Name, node.mod.Id)
		case *MappingElement:
			fmt.Fprintf(b.output, "{{{mmap:0x%x:0x%x:load:%d:%s:0x%x}}}", node.seg.Vaddr, node.seg.Size, node.seg.Mod, node.seg.Flags, node.seg.ModRelAddr)
		}
	}
	fmt.Fprintf(b.output, "\n")
}
