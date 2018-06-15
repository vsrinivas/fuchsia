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

	"fuchsia.googlesource.com/tools/elflib"
)

// Symbolizer is an interface to an object that maps addresses in a bianry to source locations
type Symbolizer interface {
	FindSrcLoc(file, build string, modRelAddr uint64) <-chan LLVMSymbolizeResult
}

// TODO (jakehehrlich): Consider add BinaryFileRef here.
type llvmSymboArgs struct {
	file       string
	build      string
	modRelAddr uint64
	output     chan LLVMSymbolizeResult
}

type LLVMSymbolizeResult struct {
	Locs []SourceLocation
	Err  error
}

type LLVMSymbolizer struct {
	path       string
	stdin      io.WriteCloser
	stdout     io.ReadCloser
	symbolizer *exec.Cmd
	input      chan llvmSymboArgs
}

func NewLLVMSymbolizer(llvmSymboPath string) *LLVMSymbolizer {
	var out LLVMSymbolizer
	out.path = llvmSymboPath
	out.symbolizer = exec.Command(llvmSymboPath)
	out.input = make(chan llvmSymboArgs)
	return &out
}

func unknownStr(str string) OptStr {
	if str == "??" || str == "" {
		return EmptyOptStr()
	}
	return NewOptStr(str)
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
			fmt.Fprintf(s.stdin, "%s 0x%x\n", args.file, args.modRelAddr)
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
			args.output <- LLVMSymbolizeResult{out, nil}
		}
	}
}

func (s *LLVMSymbolizer) Start(ctx context.Context) error {
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
	go s.handle(ctx)
	return nil
}

func (s *LLVMSymbolizer) FindSrcLoc(file, build string, modRelAddr uint64) <-chan LLVMSymbolizeResult {
	// Buffer the return chanel so we don't block handle().
	out := make(chan LLVMSymbolizeResult, 1)
	args := llvmSymboArgs{file, build, modRelAddr, out}
	s.input <- args
	return out
}
