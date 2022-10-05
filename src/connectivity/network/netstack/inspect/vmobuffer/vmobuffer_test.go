// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package vmobuffer

import (
	"bytes"
	"fmt"
	"io"
	"math/rand"
	"reflect"
	"testing"
	"testing/quick"

	"go.fuchsia.dev/fuchsia/src/lib/component"

	"github.com/google/go-cmp/cmp"
)

type BufferWriteCmd struct {
	Buf []byte
}

func generateBufferWriteCmd(rand *rand.Rand, size int) BufferWriteCmd {
	buf := make([]byte, size*rand.Intn(10))
	// rand.Read is documented to always return (len(buf), nil).
	n, err := rand.Read(buf)
	if n != len(buf) || err != nil {
		panic(fmt.Sprintf("rand.Read(_) = %d, %s", n, err))
	}
	return BufferWriteCmd{Buf: buf}
}

type BufferWriteCmdSequence []BufferWriteCmd

var _ quick.Generator = (BufferWriteCmdSequence)(nil)

func (BufferWriteCmdSequence) Generate(rand *rand.Rand, size int) reflect.Value {
	numCommands := size
	var commands []BufferWriteCmd
	for i := 0; i < numCommands; i++ {
		commands = append(commands, generateBufferWriteCmd(rand, size))
	}
	return reflect.ValueOf(BufferWriteCmdSequence(commands))
}

type BufferReadCmd struct {
	Read   *int
	ReadAt *struct {
		Len    int
		Offset int
	}
	Seek *struct {
		Offset int
		Whence int
	}
}

func generateBufferReadCmd(rand *rand.Rand, size int) BufferReadCmd {
	choice := rand.Intn(3*size) % 3
	switch choice {
	case 0:
		readLen := rand.Intn(10 * size)
		return BufferReadCmd{Read: &readLen}
	case 1:
		return BufferReadCmd{
			ReadAt: &struct {
				Len    int
				Offset int
			}{
				Len:    rand.Intn(10 * size),
				Offset: rand.Intn(10 * size),
			}}
	case 2:
		return BufferReadCmd{
			Seek: &struct {
				Offset int
				Whence int
			}{
				Offset: rand.Intn(10*size) * (-(rand.Intn(size) % 2)),
				Whence: []int{io.SeekCurrent, io.SeekEnd, io.SeekStart}[rand.Intn(3)],
			},
		}
	default:
		panic("unreachable")
	}
}

type BufferReadCmdSequence []BufferReadCmd

var _ quick.Generator = (BufferReadCmdSequence)(nil)

func (BufferReadCmdSequence) Generate(rand *rand.Rand, size int) reflect.Value {
	numCommands := size
	var commands []BufferReadCmd
	for i := 0; i < numCommands; i++ {
		commands = append(commands, generateBufferReadCmd(rand, size))
	}
	return reflect.ValueOf(BufferReadCmdSequence(commands))
}

type WriteResult struct {
	NumBytesWritten int
	Error           error
}

func executeWrites(writer io.Writer, writes BufferWriteCmdSequence) []string {
	var writeResults []string
	for _, write := range []BufferWriteCmd(writes) {
		n, err := writer.Write(write.Buf)
		writeResults = append(writeResults, fmt.Sprintf("Write(%d, %s)", n, err))
	}

	return writeResults
}

func executeReads(reader component.ReaderWithoutCloser, reads BufferReadCmdSequence) []string {
	var readResults []string
	for _, read := range []BufferReadCmd(reads) {
		if read.Read != nil {
			p := make([]byte, *read.Read)
			n, err := reader.Read(p)
			readResults = append(readResults, fmt.Sprintf("Read(n:%d, eof: %t, err:%t, output:%s)", n, err == io.EOF, err != nil, string(p)))
		}
		if read.ReadAt != nil {
			p := make([]byte, read.ReadAt.Len)
			n, err := reader.ReadAt(p, int64(read.ReadAt.Offset))
			readResults = append(readResults, fmt.Sprintf("ReadAt(n:%d, eof: %t, err: %t, output:%s)", n, err == io.EOF, err != nil, string(p)))
		}
		if read.Seek != nil {
			n, err := reader.Seek(int64(read.Seek.Offset), read.Seek.Whence)
			readResults = append(readResults, fmt.Sprintf("Seek(n:%d, eof: %t, err: %t)", n, err == io.EOF, err != nil))
		}
	}
	return readResults
}

func executeWithBuffer(writes BufferWriteCmdSequence, reads BufferReadCmdSequence) ([]string, []string) {
	var buf bytes.Buffer

	writeResults := executeWrites(&buf, writes)

	reader := bytes.NewReader(buf.Bytes())

	readResults := executeReads(reader, reads)

	return writeResults, readResults
}

func executeWithVmo(t *testing.T, writes BufferWriteCmdSequence, reads BufferReadCmdSequence) ([]string, []string) {
	buf, err := NewVMOBuffer(4096, "test-VMOBuffer")
	if err != nil {
		t.Error(err)
	}

	writeResults := executeWrites(buf, writes)

	reader, err := buf.ToVMOReader()
	if err != nil {
		t.Error(err)
	}

	readResults := executeReads(reader, reads)

	if err := reader.Close(); err != nil {
		t.Error(err)
	}

	return writeResults, readResults
}

// TestVmoBufferWriteThenRead tests that performing an arbitrary sequence of
// writes to a vmoBuffer, followed by transforming it into a vmoReader, and
// performing an arbitrary sequence of reads, produces identical output to
// performing the same operations with a bytes.Buffer and bytes.Reader.
func TestVmoBufferWriteThenRead(t *testing.T) {
	if err := quick.CheckEqual(
		func(writes BufferWriteCmdSequence, reads BufferReadCmdSequence) ([]string, []string) {
			return executeWithVmo(t, writes, reads)
		},
		executeWithBuffer,
		nil,
	); err != nil {
		t.Error(err)
		t.Error(cmp.Diff(err.(*quick.CheckEqualError).Out1, err.(*quick.CheckEqualError).Out2))
	}
}
