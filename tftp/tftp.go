// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package tftp

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net"
	"strconv"
	"strings"
	"time"
)

const (
	ServerPort = 33340 // tftp server port
	ClientPort = 33341 // tftp client port
)

const (
	timeout      = 2 * time.Second // default client timeout
	retries      = 5               // default number of retries
	blockSize    = 1024            // default block size
	datagramSize = 1028            // default datagram size
	windowSize   = 256             // default window size
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
	ErrorUndefined        = uint16(0) // Not defined, see error message (if any)
	ErrorFileNotFound     = uint16(1) // File not found
	ErrorAccessViolation  = uint16(2) // Access violation
	ErrorDiskFull         = uint16(3) // Disk full or allocation exceeded
	ErrorIllegalOperation = uint16(4) // Illegal TFTP operation
	ErrorUnknownID        = uint16(5) // Unknown transfer ID
	ErrorFileExists       = uint16(6) // File already exists
	ErrorNoSuchUser       = uint16(7) // No such user
	ErrorBadOptions       = uint16(8) // Bad options

	// ErrorBusy is a Fuchsia-specific extension to the set of TFTP error
	// codes, meant to indicate that the server cannot currently handle a
	// request, but may be able to at some future time.
	ErrorBusy = uint16(0x143) // 'B' + 'U' + 'S' + 'Y'
)

var (
	ErrShouldWait = fmt.Errorf("target is busy")
)

type Client struct {
	Timeout    time.Duration // duration to wait for the client to ack a packet
	Retries    int           // how many times a packet will be resent
	BlockSize  uint16        // maximum block size used for file transfers
	WindowSize uint16        // window size used for file transfers.
}

func NewClient() *Client {
	return &Client{
		Timeout:    timeout,
		Retries:    retries,
		BlockSize:  blockSize,
		WindowSize: windowSize,
	}
}

type options struct {
	timeout      time.Duration
	blockSize    uint16
	windowSize   uint16
	transferSize int64
}

type Session interface {
	// Size returns the transfer size, if set.
	Size() int64

	// RemoteAddr returns the remote adderss.
	RemoteAddr() net.UDPAddr
}

// Send sends the filename of length size to server addr with the content being
// read from the reader.
//
// If the server being talked to is running on Fuchsia, this method may return
// ErrShouldWait, which indicates that the server is currently not ready to
// handle a new request, but will be in the future. This case should be
// explicitly handled by the caller.
func (c *Client) Send(addr *net.UDPAddr, filename string, size int64) (io.ReaderFrom, error) {
	conn, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv6zero})
	if err != nil {
		return nil, fmt.Errorf("creating socket: %s", err)
	}

	var b bytes.Buffer
	writeRequest(&b, opWrq, filename, &options{
		timeout:      c.Timeout,
		blockSize:    c.BlockSize,
		windowSize:   c.WindowSize,
		transferSize: size,
	})

	opts, addr, err := c.sendRequest(conn, addr, b.Bytes())
	if err != nil {
		conn.Close()
		if err == ErrShouldWait {
			return nil, ErrShouldWait
		}
		return nil, fmt.Errorf("sending WRQ: %s", err)
	}
	b.Reset()
	if opts.transferSize != size {
		err := errors.New("transfer size mismatch")
		abort(conn, addr, ErrorBadOptions, err)
		return nil, err
	}

	return &sender{
		addr:  addr,
		conn:  conn,
		opts:  opts,
		retry: c.Retries,
	}, nil
}

type sender struct {
	addr  *net.UDPAddr
	conn  *net.UDPConn
	opts  *options
	retry int
}

func (s *sender) Size() (n int64) {
	return s.opts.transferSize
}

func (s *sender) RemoteAddr() net.UDPAddr {
	return *s.addr
}

func (s *sender) ReadFrom(r io.Reader) (int64, error) {
	l, err := s.send(r)
	if err != nil {
		abort(s.conn, s.addr, ErrorUndefined, err)
		return l, err
	}
	s.conn.Close()
	return l, nil
}

func (s *sender) send(r io.Reader) (int64, error) {
	var n int64    // total number of bytes read
	var seq uint64 // sequence number denoting the beginning of window
	b := make([]byte, s.opts.blockSize+4)
	ra, ok := r.(io.ReaderAt)
	if !ok {
		return n, fmt.Errorf("%t does not implement io.ReaderAt", r)
	}

Loop:
	for {
	Attempt:
		for attempt := 0; attempt < s.retry; attempt++ {
			for i := uint16(0); uint16(i) < s.opts.windowSize; i++ {
				block := uint16(seq + uint64(i+1))
				// Fill the first byte of the command with an arbitrary value to
				// work around the issue where certain ethernet adapters would
				// refuse to send the packet on retransmission.
				b[0] = uint8(attempt)
				b[1] = opData
				binary.BigEndian.PutUint16(b[2:], block)
				off := (seq + uint64(i)) * uint64(s.opts.blockSize)
				l, err := ra.ReadAt(b[4:], int64(off))
				n += int64(l)
				if err != nil && err != io.EOF {
					return n, fmt.Errorf("reading bytes for block %d: %s", block, err)
				}
				isEOF := err == io.EOF
				if _, err := s.conn.WriteToUDP(b[:l+4], s.addr); err != nil {
					return n, fmt.Errorf("sending block %d: %s", block, err)
				}
				if isEOF {
					break
				}
			}

			s.conn.SetReadDeadline(time.Now().Add(s.opts.timeout))

			for {
				m, addr, err := s.conn.ReadFromUDP(b[:])
				if err != nil {
					if t, ok := err.(net.Error); ok && t.Timeout() {
						continue Attempt
					}
					return n, err
				}
				if m < 4 { // packet too small
					continue
				}
				if !addr.IP.Equal(s.addr.IP) || addr.Port != s.addr.Port {
					continue
				}
				break
			}

			switch b[1] {
			case opAck:
				num := binary.BigEndian.Uint16(b[2:4])
				off := num - uint16(seq) // offset from the start of the window
				if off > 0 && off <= s.opts.windowSize {
					seq += uint64(off)
					if seq*uint64(s.opts.blockSize) >= uint64(s.opts.transferSize) {
						return n, nil // all data transferred, return number of bytes
					}
				}
				continue Loop
			case opError:
				msg, _, _ := netasciiString(b[4:])
				return n, fmt.Errorf("server aborted transfer: %s", msg)
			}
		}
		return n, errors.New("timeout waiting for ACK")
	}
}

// Receive reads the data from filename at the server addr with the content
// being written to the writer.
//
// If the server being talked to is running on a Fuchsia, this method may return
// ErrShouldWait, which indicates that the server is currently not ready to
// handle a new request, but will be in the future. This case should be
// explicitly handled by the caller.
func (c *Client) Receive(addr *net.UDPAddr, filename string) (io.WriterTo, error) {
	conn, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv6zero})
	if err != nil {
		return nil, fmt.Errorf("creating socket: %s", err)
	}

	var b bytes.Buffer
	writeRequest(&b, opRrq, filename, &options{
		blockSize:  c.BlockSize,
		timeout:    c.Timeout,
		windowSize: c.WindowSize,
	})
	opts, addr, err := c.sendRequest(conn, addr, b.Bytes())
	if err != nil {
		conn.Close()
		if err == ErrShouldWait {
			return nil, ErrShouldWait
		}
		return nil, fmt.Errorf("sending RRQ: %s", err)
	}
	b.Reset()
	if err := acknowledge(conn, addr, uint16(0)); err != nil {
		return nil, fmt.Errorf("sending ACK: %s", err)
	}

	return &receiver{
		addr:  addr,
		conn:  conn,
		opts:  opts,
		retry: c.Retries,
	}, nil
}

type receiver struct {
	addr  *net.UDPAddr
	conn  *net.UDPConn
	opts  *options
	retry int
}

func (r *receiver) Size() (n int64) {
	return r.opts.transferSize
}

func (r *receiver) RemoteAddr() net.UDPAddr {
	return *r.addr
}

func (r *receiver) WriteTo(w io.Writer) (int64, error) {
	l, err := r.receive(w)
	if err != nil {
		abort(r.conn, r.addr, ErrorUndefined, err)
		return l, err
	}
	r.conn.Close()
	return l, nil
}

func (r *receiver) receive(w io.Writer) (int64, error) {
	var n int64
	var seq uint64
	recv := make([]byte, r.opts.blockSize+4)

Loop:
	for {
	Attempt:
		for attempt := 0; attempt < r.retry; attempt++ {
			for i := uint16(0); i < uint16(r.opts.windowSize); i++ {
				r.conn.SetReadDeadline(time.Now().Add(r.opts.timeout))

				var m int
				for {
					var addr *net.UDPAddr
					var err error
					m, addr, err = r.conn.ReadFromUDP(recv[m:])
					if err != nil {
						if t, ok := err.(net.Error); ok && t.Timeout() {
							acknowledge(r.conn, r.addr, uint16(seq))
							continue Attempt
						}
						return n, err
					}
					if m < 4 { // packet too small
						continue
					}
					if !addr.IP.Equal(r.addr.IP) || addr.Port != r.addr.Port {
						continue
					}
					break
				}

				n += int64(m)

				switch recv[1] {
				case opData:
					if num := binary.BigEndian.Uint16(recv[2:4]); num != uint16(seq)+1 {
						if num-uint16(seq) < uint16(r.opts.windowSize) {
							acknowledge(r.conn, r.addr, uint16(seq))
						}
						continue Loop
					}
					seq++
					if _, err := w.Write(recv[4:m]); err != nil {
						return n, fmt.Errorf("writing bytes for block %d: %v", seq, err)
					}
					if m < int(r.opts.blockSize+4) {
						acknowledge(r.conn, r.addr, uint16(seq))
						return n, nil
					}
				case opError:
					msg, _, _ := netasciiString(recv[4:])
					return n, fmt.Errorf("server aborted transfer: %s", msg)
				}
			}
			// TODO: this should be addr
			acknowledge(r.conn, r.addr, uint16(seq))
			continue Loop
		}
		return n, errors.New("timeout waiting for DATA")
	}
}

func writeRequest(b *bytes.Buffer, op uint8, filename string, o *options) {
	b.WriteByte(0)
	b.WriteByte(op)
	writeString(b, filename)
	// Only support octet mode, because in practice that's the
	// only remaining sensible use of TFTP.
	writeString(b, "octet")
	writeOption(b, "tsize", o.transferSize)
	writeOption(b, "blksize", int64(o.blockSize))
	writeOption(b, "timeout", int64(o.timeout/time.Second))
	writeOption(b, "windowsize", int64(o.windowSize))
}

func writeOption(b *bytes.Buffer, name string, value int64) {
	writeString(b, name)
	writeString(b, strconv.FormatInt(value, 10))
}

func writeString(b *bytes.Buffer, s string) {
	b.WriteString(s)
	b.WriteByte(0)
}

func (c *Client) sendRequest(conn *net.UDPConn, addr *net.UDPAddr, b []byte) (*options, *net.UDPAddr, error) {
	for attempt := 0; attempt < c.Retries; attempt++ {
		if _, err := conn.WriteToUDP(b, addr); err != nil {
			return nil, nil, err
		}

		conn.SetReadDeadline(time.Now().Add(c.Timeout))

		var recv [256]byte
		for {
			n, addr, err := conn.ReadFromUDP(recv[:])
			if err != nil {
				if t, ok := err.(net.Error); ok && t.Timeout() {
					break
				}
				return nil, nil, err
			}

			if n < 4 { // packet too small
				continue
			}
			switch recv[1] {
			case opError:
				// Handling ErrorBusy here is a Fuchsia-specific extension.
				if code := binary.BigEndian.Uint16(recv[2:4]); code == ErrorBusy {
					return nil, addr, ErrShouldWait
				}
				msg, _, _ := netasciiString(recv[4:n])
				return nil, addr, fmt.Errorf("server aborted transfer: %s", msg)
			case opOack:
				options, err := parseOACK(recv[2:n])
				return options, addr, err
			}
		}
	}

	return nil, nil, errors.New("timeout waiting for OACK")
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
			o.transferSize = int64(size)
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

func acknowledge(conn *net.UDPConn, addr *net.UDPAddr, seq uint16) error {
	var b bytes.Buffer
	b.WriteByte(0)
	b.WriteByte(opAck)
	if err := binary.Write(&b, binary.BigEndian, seq); err != nil {
		return fmt.Errorf("writing seqnum: %v", err)
	}
	if _, err := conn.WriteToUDP(b.Bytes(), addr); err != nil {
		return err
	}
	return nil
}

func abort(conn *net.UDPConn, addr *net.UDPAddr, code uint16, err error) error {
	var b bytes.Buffer
	b.WriteByte(0)
	b.WriteByte(opError)
	if binary.Write(&b, binary.BigEndian, code); err != nil {
		return fmt.Errorf("writing code: %v", err)
	}
	b.WriteString(err.Error())
	b.WriteByte(0)
	if _, err := conn.WriteToUDP(b.Bytes(), addr); err != nil {
		return err
	}
	conn.Close()
	return nil
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

// WindowReader supports reading bytes from an underlying stream using
// fixed sized blocks and rewinding back up to slots blocks.
type WindowReader struct {
	buf     []byte    // buffer space
	len     []int     // len of data written to each slot
	reader  io.Reader // underlying reader object
	current int       // current block to be read
	head    int       // head of buffer
	slots   int       // number of slots
	size    int       // size of the slot
}

// NewWindowReader creates a new reader with slots blocks of size.
func NewWindowReader(reader io.Reader, slots int, size int) *WindowReader {
	return &WindowReader{
		buf:    make([]byte, slots*size),
		len:    make([]int, slots),
		reader: reader,
		slots:  slots,
		size:   size,
	}
}

func (r *WindowReader) Read(p []byte) (int, error) {
	slot := r.current % r.slots
	offset := slot * r.size

	if r.current != r.head {
		len := offset + r.len[slot]
		n := copy(p, r.buf[offset:len])
		r.current++
		return n, nil
	}

	n, err := r.reader.Read(p)
	if err != nil {
		return n, err
	}
	n = copy(r.buf[offset:offset+n], p[:n])
	r.len[slot] = n

	r.current++
	r.head = r.current
	return n, err
}

// Unread rewinds the reader back by n blocks.
func (r *WindowReader) Unread(n int) {
	r.current -= n
}
