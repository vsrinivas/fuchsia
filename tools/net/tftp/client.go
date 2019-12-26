// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tftp

import (
	"bytes"
	"context"
	"encoding/binary"
	"fmt"
	"io"
	"math"
	"net"
	"sync"
)

// Client is used to Read or Write files to/from a TFTP remote.
type Client interface {
	newTransfer(opCode uint8, filename string) *transfer
	Read(ctx context.Context, filename string) (*bytes.Reader, error)
	Write(ctx context.Context, filename string, reader io.ReaderAt, size int64) error
	RemoteAddr() *net.UDPAddr
}

// ClientImpl implements the Client interface; it is exported for testing.
type ClientImpl struct {
	addr *net.UDPAddr
	conn *net.UDPConn
	mu   *sync.Mutex
}

// NewClient returns a Client which can be used to Read or Write
// files to/from a TFTP remote.
func NewClient(addr *net.UDPAddr) (*ClientImpl, error) {
	conn, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv6zero})
	if err != nil {
		return nil, err
	}
	return &ClientImpl{
		addr: addr,
		conn: conn,
		mu:   &sync.Mutex{},
	}, nil
}

func (c *ClientImpl) newTransfer(opCode uint8, filename string) *transfer {
	t := &transfer{
		addr:     c.addr,
		buffer:   bytes.NewBuffer([]byte{}),
		client:   c,
		filename: filename,
		opCode:   opCode,
		opts: &options{
			timeout:    timeout,
			blockSize:  blockSize,
			windowSize: windowSize,
		},
	}
	return t
}

// Read requests to read a file from the TFTP remote, if the request is successful,
// the contents of the remote file are returned as a bytes.Reader, if the request
// is unsuccessful an error is returned, if the error is ErrShouldWait, the request
// can be retried at some point in the future.
func (c *ClientImpl) Read(ctx context.Context, filename string) (*bytes.Reader, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	t := c.newTransfer(opRrq, filename)
	attempts := 0
	transferStarted := false
	for {
		if !transferStarted {
			// Send the request to read the file.
			err := t.request()
			if err != nil {
				return nil, err
			}
		}
		recv, err := t.wait(ctx, expectAny)
		transferStarted = transferStarted || recv == opOack || recv == opData
		if err != nil {
			if err, ok := err.(net.Error); ok && err.Timeout() {
				attempts++
				if attempts < retries {
					if !transferStarted {
						t.cancel(errorUndefined, err)
					}
					continue
				}
				return nil, err
			}
			if err == io.EOF {
				return bytes.NewReader(t.buffer.Bytes()), nil
			}
			t.cancel(errorUndefined, err)
			return nil, err
		}
		if ctx.Err() != nil {
			t.cancel(errorUndefined, ctx.Err())
			return nil, ctx.Err()
		}
	}
}

func (c *ClientImpl) RemoteAddr() *net.UDPAddr {
	return c.addr
}

// Write requests to send a file to the TFTP remote, if the operation is unsuccesful
// error is returned, if the error is ErrShouldWait, the request can be retried at
// some point in the future.
func (c *ClientImpl) Write(ctx context.Context, filename string, r io.ReaderAt, size int64) error {
	c.mu.Lock()
	defer c.mu.Unlock()
	t := c.newTransfer(opWrq, filename)
	attempts := 0
	t.opts.transferSize = uint64(size)
	// Send the request to write the file.
	for {
		if ctx.Err() != nil {
			return ctx.Err()
		}

		err := t.request()
		if err != nil {
			return err
		}
		// Wait for the receiving side to ACK options.
		if _, err := t.wait(ctx, expectOack); err != nil {
			t.cancel(errorUndefined, err)
			if err, ok := err.(net.Error); ok && err.Timeout() {
				attempts++
				if attempts < retries {
					continue
				}
				return err
			}
			return err
		}
		break
	}

	// Send the file.
	for {
		for i := uint32(0); i < uint32(t.opts.windowSize); i++ {
			b := make([]byte, t.opts.blockSize+dataOffset)
			block := t.seq + i + 1
			b[1] = opData
			binary.BigEndian.PutUint16(b[2:], uint16(math.MaxUint16&block))
			off := int64(block-1) * int64(t.opts.blockSize)
			n, err := r.ReadAt(b[dataOffset:], off)
			if err != nil && err != io.EOF {
				t.cancel(errorUndefined, err)
				return fmt.Errorf("reading bytes for block %d: %s", block, err)
			}
			isEOF := err == io.EOF
			if err := t.send(b[:n+dataOffset]); err != nil {
				t.cancel(errorUndefined, err)
				return fmt.Errorf("sending block %d: %s", block, err)
			}
			// We transfered all data.  Break & wait for ACK.
			if isEOF {
				break
			}
		}
		// Wait for the receiving side to ACK or possibly error out.
		if _, err := t.wait(ctx, expectAck); err != nil {
			if err, ok := err.(net.Error); ok && err.Timeout() {
				continue
			}
			return err
		}
		// The full transfer has been ACK'd. Finished.
		if uint64(t.seq)*uint64(t.opts.blockSize) > t.opts.transferSize {
			return nil
		}
		if ctx.Err() != nil {
			t.cancel(errorUndefined, ctx.Err())
			return ctx.Err()
		}
	}
}
