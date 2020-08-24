// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tftp

import (
	"bytes"
	"errors"
	"fmt"
	"strconv"
	"strings"
	"time"
)

const (
	ServerPort = 33340 // tftp server port
	ClientPort = 33341 // tftp client port
)

const (
	// Ethernet MTU, less the TFTP, UDP and IP header lengths (RFC 2348).
	blockSize = 1428 // default block size
	// Arbitrary.
	timeout = 1 * time.Second // default udp client timeout
	// Arbitrary.
	retries = 8 // default number of timeout retries
	// Point at where window size gains begin to tail off (RFC 7440)
	// A higher window size begins to cause increased NACKs over
	// lossy connections.
	windowSize = 16 // default window size
)

const (
	opRrq   = uint8(1) // read request (RRQ)
	opWrq   = uint8(2) // write request (WRQ)
	opData  = uint8(3) // data
	opAck   = uint8(4) // acknowledgement
	opError = uint8(5) // error
	opOack  = uint8(6) // option acknowledgment
)

const (
	errorUndefined        = uint16(0) // Not defined, see error message (if any)
	errorFileNotFound     = uint16(1) // File not found
	errorAccessViolation  = uint16(2) // Access violation
	errorDiskFull         = uint16(3) // Disk full or allocation exceeded
	errorIllegalOperation = uint16(4) // Illegal TFTP operation
	errorUnknownID        = uint16(5) // Unknown transfer ID
	errorFileExists       = uint16(6) // File already exists
	errorNoSuchUser       = uint16(7) // No such user
	errorBadOptions       = uint16(8) // Bad options

	// ErrorBusy is a Fuchsia-specific extension to the set of TFTP error
	// codes, meant to indicate that the server cannot currently handle a
	// request, but may be able to at some future time.
	errorBusy = uint16(0x143) // 'B' + 'U' + 'S' + 'Y'
)

const (
	opCodeOffset      = 1 // op code offset in packet
	blockNumberOffset = 2 // block number offset in packet
	dataOffset        = 4 // data start in packet
)

var (
	// ErrShouldWait indicates that the target is busy.
	ErrShouldWait = fmt.Errorf("target is busy")
)

type options struct {
	blockSize    uint16
	timeout      time.Duration
	transferSize uint64
	windowSize   uint16
}

func writeOption(b *bytes.Buffer, name string, value int64) {
	writeString(b, name)
	writeString(b, strconv.FormatInt(value, 10))
}

func writeString(b *bytes.Buffer, s string) {
	b.WriteString(s)
	b.WriteByte(0)
}

func parseOACK(bs []byte) (*options, error) {
	var o options

	for len(bs) > 0 {
		opt, rest, err := netasciiString(bs)
		if err != nil {
			return nil, fmt.Errorf("reading option name: %s", err)
		}
		bs = rest
		val, rest, err := netasciiString(bs)
		if err != nil {
			return nil, fmt.Errorf("reading option %q value: %s", opt, err)
		}
		bs = rest
		switch strings.ToLower(opt) {
		case "blksize":
			size, err := strconv.ParseUint(val, 10, 16)
			if err != nil {
				return nil, fmt.Errorf("unsupported block size %q", val)
			}
			o.blockSize = uint16(size)
		case "timeout":
			seconds, err := strconv.ParseUint(val, 10, 8)
			if err != nil {
				return nil, fmt.Errorf("unsupported timeout %q", val)
			}
			o.timeout = time.Second * time.Duration(seconds)
		case "tsize":
			size, err := strconv.ParseUint(val, 10, 64)
			if err != nil {
				return nil, fmt.Errorf("unsupported transfer size %q", val)
			}
			o.transferSize = uint64(size)
		case "windowsize":
			size, err := strconv.ParseUint(val, 10, 16)
			if err != nil {
				return nil, fmt.Errorf("unsupported window size %q", val)
			}
			o.windowSize = uint16(size)
		}
	}

	return &o, nil
}

func netasciiString(bs []byte) (string, []byte, error) {
	for i, b := range bs {
		if b == 0 {
			return string(bs[:i]), bs[i+1:], nil
		} else if b < 0x20 || b > 0x7e {
			return "", nil, fmt.Errorf("invalid netascii byte %q at offset %d", b, i)
		}
	}
	return "", nil, errors.New("no null terminated string found")
}
