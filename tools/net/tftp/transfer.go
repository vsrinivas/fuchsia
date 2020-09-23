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
	"time"
)

type transfer struct {
	addr     *net.UDPAddr
	buffer   *bytes.Buffer
	filename string
	lastAck  uint32
	opCode   uint8
	seq      uint32
	opts     *options
	client   *ClientImpl
}

func (t *transfer) request() error {
	b := &bytes.Buffer{}
	b.WriteByte(0)
	b.WriteByte(t.opCode)
	writeString(b, t.filename)
	writeString(b, "octet")
	writeOption(b, "tsize", int64(t.opts.transferSize))
	writeOption(b, "blksize", int64(t.opts.blockSize))
	writeOption(b, "timeout", int64(t.opts.timeout/time.Second))
	writeOption(b, "windowsize", int64(t.opts.windowSize))

	return t.send(b.Bytes())
}

func (t *transfer) wait(ctx context.Context, handlePacket func(*transfer, []byte, *net.UDPAddr) error) (uint8, error) {
	b := make([]byte, t.opts.blockSize+dataOffset)
	done := make(chan struct{})
	defer close(done)
	go func() {
		for {
			select {
			case <-ctx.Done():
				t.client.conn.SetReadDeadline(time.Now())
				time.Sleep(time.Second)
				continue
			case <-done:
				return
			}
		}
	}()
	for {
		t.client.conn.SetReadDeadline(time.Now().Add(t.opts.timeout))
		n, addr, err := t.client.conn.ReadFromUDP(b)
		if err != nil {
			if err, ok := err.(net.Error); ok && err.Timeout() {
				if ctx.Err() != nil {
					return 0, ctx.Err()
				}
				return 0, err
			}
			t.cancel(errorUndefined, err)
			return 0, err
		}
		if n < dataOffset {
			continue
		}
		if !addr.IP.Equal(t.addr.IP) {
			continue
		}
		return b[opCodeOffset], handlePacket(t, b[:n], addr)
	}
}

func expectData(t *transfer, b []byte, addr *net.UDPAddr) error {
	switch b[opCodeOffset] {
	case opData:
		return t.handleData(b)
	case opError:
		return t.handleError(b)
	default:
		return handleUnexpected("data", b)
	}
}

func expectAck(t *transfer, b []byte, addr *net.UDPAddr) error {
	switch b[opCodeOffset] {
	case opAck:
		return t.handleAck(b)
	case opError:
		return t.handleError(b)
	default:
		return handleUnexpected("ack", b)
	}
}

func expectAny(t *transfer, b []byte, addr *net.UDPAddr) error {
	switch b[opCodeOffset] {
	case opAck:
		return t.handleAck(b)
	case opData:
		return t.handleData(b)
	case opError:
		return t.handleError(b)
	case opOack:
		return t.handleOack(b, addr)
	default:
		return handleUnexpected("any", b)
	}
}

func expectOack(t *transfer, b []byte, addr *net.UDPAddr) error {
	switch b[opCodeOffset] {
	case opError:
		return t.handleError(b)
	case opOack:
		return t.handleOack(b, addr)
	default:
		return handleUnexpected("oack", b)
	}
}

func (t *transfer) ack(seq uint32) error {
	var b bytes.Buffer
	b.WriteByte(0)
	b.WriteByte(opAck)
	if err := binary.Write(&b, binary.BigEndian, uint16(math.MaxUint16&seq)); err != nil {
		return fmt.Errorf("ack: %v", err)
	}

	return t.send(b.Bytes())
}

func (t *transfer) cancel(code uint16, err error) error {
	var b bytes.Buffer
	b.WriteByte(0)
	b.WriteByte(opError)
	if err2 := binary.Write(&b, binary.BigEndian, code); err2 != nil {
		return fmt.Errorf("cancel: %v", err2)
	}
	writeString(&b, err.Error())
	return t.send(b.Bytes())
}

func (t *transfer) send(buf []byte) error {
	t.client.conn.SetWriteDeadline(time.Now().Add(t.opts.timeout))
	if _, err := t.client.conn.WriteToUDP(buf, t.addr); err != nil {
		return fmt.Errorf("send: %v", err)
	}
	return nil
}

func (t *transfer) handleData(p []byte) error {
	seq := binary.BigEndian.Uint16(p[blockNumberOffset:dataOffset])
	// If this isn't the next packet we expect, resend an ACK
	// for the last in-order packet received.
	if seq != uint16(t.seq)+1 {
		if seq-uint16(t.seq) <= t.opts.windowSize {
			if err := t.ack(t.seq); err != nil {
				return err
			}
			t.lastAck = t.seq
		}
		return nil
	}
	// Append the packet payload to the buffer.
	if _, err := t.buffer.Write(p[dataOffset:]); err != nil {
		t.cancel(errorUndefined, err)
		return err
	}
	t.seq++
	// If the packet has a partial payload, it's the last,
	// ACK it and return.
	if len(p) < int(t.opts.blockSize+dataOffset) {
		if err := t.ack(t.seq); err != nil {
			return err
		}
		t.lastAck = t.seq
		return io.EOF
	}
	// If this is the last packet in the receive window,
	// ACK it.
	if uint16(t.seq-t.lastAck) == t.opts.windowSize {
		if err := t.ack(t.seq); err != nil {
			return err
		}
		t.lastAck = t.seq
	}
	return nil
}

func (t *transfer) handleAck(p []byte) error {
	seq := binary.BigEndian.Uint16(p[blockNumberOffset:dataOffset])
	off := seq - uint16(t.seq)
	if off > 0 && off <= t.opts.windowSize {
		t.seq += uint32(off)
	}
	return nil
}

func (t *transfer) handleOack(p []byte, addr *net.UDPAddr) error {
	o, err := parseOACK(p[blockNumberOffset:])
	if err != nil {
		t.cancel(errorBadOptions, err)
		return err
	}

	if t.opts.transferSize != 0 && t.opts.transferSize != o.transferSize {
		err := fmt.Errorf("transfer size mismatch")
		t.cancel(errorBadOptions, err)
		return err
	}
	t.opts = o
	t.addr = addr
	// Rrq requires an ACK of the OACK.
	if t.opCode == opRrq {
		return t.ack(t.seq)
	}
	return nil
}

func (t *transfer) handleError(p []byte) error {
	if errCode := binary.BigEndian.Uint16(p[blockNumberOffset:dataOffset]); errCode == errorBusy {
		return ErrShouldWait
	}
	msg, _, err := netasciiString(p[dataOffset:])
	if err != nil {
		return fmt.Errorf("could not parse server cancellation message: %s", err)
	}
	return fmt.Errorf("server canceled transfer: %s", msg)
}

func handleUnexpected(expected string, p []byte) error {
	// This should never happen.  This represents the client and
	// server having mismatched state and a very real bug somewhere.
	// But this happens. fxbug.dev/42283. We'll log and try to recover the best we
	// can.
	return fmt.Errorf("unexpected opcode! (expected %s)\n%v", expected, p)
}
