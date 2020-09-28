// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"os/exec"
	"strconv"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/debug/elflib"
	"go.fuchsia.dev/fuchsia/tools/lib/cache"
)

// Symbolizer is an interface to an object that maps addresses in a bianry to source locations
type Symbolizer interface {
	FindSrcLoc(file, build string, modRelAddr uint64) <-chan LLVMSymbolizeResult
}

type llvmSymbolizeArgsKey struct {
	build      string
	modRelAddr uint64
}

type llvmSymboArgs struct {
	file       string
	build      string
	modRelAddr uint64
	output     chan LLVMSymbolizeResult
}

const maxCacheSize = 2048

type LLVMSymbolizeResult struct {
	Locs []SourceLocation
	Err  error
}

type LLVMSymbolizer struct {
	path            string
	stdin           io.WriteCloser
	stdout          io.ReadCloser
	symbolizer      *exec.Cmd
	input           chan llvmSymboArgs
	cache           cache.Cache
	restartInterval uint
	restartCounter  uint
}

func NewLLVMSymbolizer(llvmSymboPath string, restartInterval uint) *LLVMSymbolizer {
	var out LLVMSymbolizer
	out.path = llvmSymboPath
	out.input = make(chan llvmSymboArgs)
	out.cache = &cache.LRUCache{Size: maxCacheSize}
	// TODO(fxbug.dev/42018): llvm-symbolizer can use *tons* of memory so we restart it.
	// Once it no longer uses up so much memory, remove this.
	out.restartInterval = restartInterval
	out.restartCounter = 0
	return &out
}

func unknownStr(str string) OptStr {
	if str == "??" || str == "" {
		return EmptyOptStr()
	}
	return NewOptStr(str)
}

func restartSymbolizerImpl(s *LLVMSymbolizer) error {
	if err := s.stdin.Close(); err != nil {
		return err
	}
	if err := s.symbolizer.Wait(); err != nil {
		return err
	}
	return s.start()
}

// Indirection for testability
var restartSymbolizer = restartSymbolizerImpl

func (s *LLVMSymbolizer) restartIfNeeded() bool {
	if s.restartInterval == 0 {
		return false
	}
	restarted := false
	if s.restartCounter == s.restartInterval-1 {
		if err := restartSymbolizer(s); err != nil {
			panic("restarting llvm-symbolizer failed: " + fmt.Sprintf("%v", err))
		}
		restarted = true
	}
	s.restartCounter = (s.restartCounter + 1) % s.restartInterval

	return restarted
}

func (s *LLVMSymbolizer) handle(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return
		case args, ok := <-s.input:
			if !ok {
				return
			}
			// See if we've seen this before and send off the result
			key := llvmSymbolizeArgsKey{args.build, args.modRelAddr}
			if res, ok := s.cache.Get(key); ok {
				args.output <- res.(LLVMSymbolizeResult)
				continue
			}
			if len(strings.TrimSpace(args.file)) == 0 {
				args.output <- LLVMSymbolizeResult{
					nil, fmt.Errorf("Attempt to request code location of unnamed file with build ID %x", args.build)}
				continue
			}
			// Before sending a binary off to llvm-symbolizer, verify the binary
			if err := elflib.NewBinaryFileRef(args.file, args.build).Verify(); err != nil {
				args.output <- LLVMSymbolizeResult{nil, err}
				continue
			}
			s.restartIfNeeded()
			// From //zircon/docs/symbolizer_markup.md:
			// In frames after frame zero, this code location identifies a call site.
			// Some emitters may subtract one byte or one instruction length from the
			// actual return address for the call site, with the intent that the address
			// logged can be translated directly to a source location for the call site
			// and not for the apparent return site thereafter (which can be confusing).
			// It‘s recommended that emitters not do this, so that each frame’s code
			// location is the exact return address given to its callee and e.g. could be
			// highlighted in instruction-level disassembly. The symbolizing filter can do
			// the adjustment to the address it translates into a source location. Assuming
			// that a call instruction is longer than one byte on all supported machines,
			// applying the "subtract one byte" adjustment a second time still results in an
			// address somewhere in the call instruction, so a little sloppiness here does
			// no harm.
			fmt.Fprintf(s.stdin, "%s 0x%x\n", args.file, args.modRelAddr-1)
			out := []SourceLocation{}
			scanner := bufio.NewScanner(s.stdout)
			for scanner.Scan() {
				function := scanner.Text()
				if len(function) == 0 {
					break
				}
				good := scanner.Scan()
				if !good {
					panic(fmt.Sprintf("%s output ended too soon", s.path))
				}
				location := scanner.Text()
				parts := strings.SplitN(location, ":", 3)
				if len(parts) < 2 {
					panic(fmt.Sprintf("%s output unrecgonized format", s.path))
				}
				line, _ := strconv.Atoi(parts[1])
				out = append(out, SourceLocation{unknownStr(parts[0]), line, unknownStr(function)})
			}
			outputRes := LLVMSymbolizeResult{out, nil}
			s.cache.Add(key, outputRes)
			args.output <- outputRes
		}
	}
}

func (s *LLVMSymbolizer) start() error {
	s.symbolizer = exec.Command(s.path)
	var err error
	if s.stdin, err = s.symbolizer.StdinPipe(); err != nil {
		return err
	}
	if s.stdout, err = s.symbolizer.StdoutPipe(); err != nil {
		return err
	}
	if err = s.symbolizer.Start(); err != nil {
		return err
	}
	return nil
}

func (s *LLVMSymbolizer) Start(ctx context.Context) error {
	if err := s.start(); err != nil {
		return err
	}
	go s.handle(ctx)
	return nil
}

func (s *LLVMSymbolizer) FindSrcLoc(file, build string, modRelAddr uint64) <-chan LLVMSymbolizeResult {
	// Buffer the return chanel so we don't block handle().
	out := make(chan LLVMSymbolizeResult, 1)
	args := llvmSymboArgs{file: file, build: build, modRelAddr: modRelAddr, output: out}
	s.input <- args
	return out
}
