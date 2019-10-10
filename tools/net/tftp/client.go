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
type Client struct {
	conn *net.UDPConn
	// RemoteAddr is the address of the TFTP remote.
	RemoteAddr *net.UDPAddr
	sync.Mutex
}

// NewClient returns a Client which can be used to Read or Write
// files to/from a TFTP remote.
func NewClient(remoteAddr *net.UDPAddr) (*Client, error) {
	conn, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv6zero})
	if err != nil {
		return nil, err
	}
	return &Client{
		conn:       conn,
		RemoteAddr: remoteAddr,
	}, nil
}

func (c *Client) newTransfer(opCode uint8, filename string) *transfer {
	t := &transfer{
		buffer:   bytes.NewBuffer([]byte{}),
		filename: filename,
		opCode:   opCode,
		options: &options{
			timeout:    timeout,
			blockSize:  blockSize,
			windowSize: windowSize,
		},
		UDPConn: c.conn,
		UDPAddr: c.RemoteAddr,
	}
	return t
}

// Read requests to read a file from the TFTP remote, if the request is successful,
// the contents of the remote file are returned as a bytes.Reader, if the request
// is unsuccessful an error is returned, if the error is ErrShouldWait, the request
// can be retried at some point in the future.
func (c *Client) Read(ctx context.Context, filename string) (*bytes.Reader, error) {
	c.Lock()
	defer c.Unlock()
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

// Write requests to send a file to the TFTP remote, if the operation is unsuccesful
// error is returned, if the error is ErrShouldWait, the request can be retried at
// some point in the future.
func (c *Client) Write(ctx context.Context, filename string, buf []byte) error {
	c.Lock()
	defer c.Unlock()
	t := c.newTransfer(opWrq, filename)
	attempts := 0
	t.transferSize = uint64(len(buf))
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

	r := bytes.NewReader(buf)

	// Send the file.
	for {
		for i := uint32(0); i < uint32(t.windowSize); i++ {
			b := make([]byte, t.blockSize+dataOffset)
			block := t.seq + i + 1
			b[1] = opData
			binary.BigEndian.PutUint16(b[2:], uint16(math.MaxUint16&block))
			off := int64(block-1) * int64(t.blockSize)
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
		if uint64(t.seq)*uint64(t.blockSize) > t.transferSize {
			return nil
		}
		if ctx.Err() != nil {
			t.cancel(errorUndefined, ctx.Err())
			return ctx.Err()
		}
	}
}
