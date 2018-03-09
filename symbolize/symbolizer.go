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
)

// Symbolizer is an interface to an object that maps addresses in a bianry to source locations
type Symbolizer interface {
	FindSrcLoc(file string, modRelAddr uint64) <-chan LLVMSymbolizeResult
}

type llvmSymboArgs struct {
	file       string
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

func (s *LLVMSymbolizer) handle(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return
		case args, ok := <-s.input:
			if !ok {
				return
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
				out = append(out, SourceLocation{parts[0], line, function})
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

func (s *LLVMSymbolizer) FindSrcLoc(file string, modRelAddr uint64) <-chan LLVMSymbolizeResult {
	// Buffer the return chanel so we don't block handle().
	out := make(chan LLVMSymbolizeResult, 1)
	args := llvmSymboArgs{file, modRelAddr, out}
	s.input <- args
	return out
}
