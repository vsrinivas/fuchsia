// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package telnet

import (
	"bufio"
	"bytes"
	"fmt"
	"io"
	"net"
	"time"
)

const (
	cmdSE    = 240
	cmdGA    = 249
	cmdSB    = 250
	cmdWill  = 251
	cmdWont  = 252
	cmdDo    = 253
	cmdDont  = 254
	cmdStart = 255

	optEcho = 1
)

const (
	DefaultPort uint16 = 23
)

// Conn represents a telnet connection.
//
// At its core, telnet is just a regular TCP connection that only
// understands ASCII and some additional command structure. The commands
// typically are used for negotiating options which both the client and the
// server support. This is done via the cmdWill, cmdWont, cmdDo, and cmdDont
// bytes which are prefixed by a cmdStart to indicates the start of a command.
// To escape a cmdStart, it must simply appear twice in a row (it escapes itself).
//
// "echo" is one such option, which indicates to the client and server to
// "echo" which is intended to indicate that "you should echo whatever is being
// written and read to the user" which typically indicates that the remote
// terminal is actually highly interactive. "echo" is common enough that a
// simple telnet client cannot get away without implementing it, so this client
// supports it.
//
// cmdGA is not generally useful unless some option provides some interactivity,
// but we don't support that (it stands for "go ahead"). More complex options
// may initiate a subnegotiation, for example terminal size. This is what cmdSB
// is for. It indicates the start of a subnegotiation, and anything that is sent
// after can be anything since its dictated by the option's formal definition.
// cmdSE indicates the end of a subnegotiation. So for terminal size this involves
// sending dimensions back and forth until the client and server agree.
//
// However, since we only support the "echo" option, then for attempts by the server
// to try to negotiate more complex options, we simply watch for the cmdSB byte and
// ignore everything after until the cmdSE byte.
type Conn struct {
	// Conn is the underlying network connection.
	net.Conn

	// reader is a buffer on top of Conn to enable
	// per-byte reading.
	reader *bufio.Reader

	// echo represents whether the echo option is enabled.
	echo bool

	// Output is a Writer which will have the raw bytes being sent
	// between this client and the server. Useful for debugging.
	Output io.Writer
}

// DialTimeout dials a telnet connection for the given host, port,
// and timeout, returning a new telnet Conn on success.
func DialTimeout(host string, port uint16, timeout time.Duration) (*Conn, error) {
	c, err := net.DialTimeout("tcp", fmt.Sprintf("%s:%d", host, port), timeout)
	if err != nil {
		return nil, err
	}
	return &Conn{
		Conn:   c,
		reader: bufio.NewReader(c),
	}, nil
}

func (c *Conn) handleOption(cmd byte, opt byte) error {
	// Ignore and deny everything that's not the echo option.
	if opt != optEcho {
		var err error
		switch cmd {
		case cmdDo, cmdDont:
			_, err = c.Conn.Write([]byte{cmdStart, cmdWont, opt})
		case cmdWill, cmdWont:
			_, err = c.Conn.Write([]byte{cmdStart, cmdDont, opt})
		}
		return err
	}

	// Handle the echo option.
	switch cmd {
	case cmdDo:
		if !c.echo {
			c.echo = true
			_, err := c.Conn.Write([]byte{cmdStart, cmdWill, opt})
			return err
		}
	case cmdDont:
		if c.echo {
			c.echo = false
			_, err := c.Conn.Write([]byte{cmdStart, cmdWont, opt})
			return err
		}
	case cmdWill:
		if !c.echo {
			c.echo = true
			_, err := c.Conn.Write([]byte{cmdStart, cmdDo, opt})
			return err
		}
	case cmdWont:
		if c.echo {
			c.echo = false
			_, err := c.Conn.Write([]byte{cmdStart, cmdDont, opt})
			return err
		}
	}
	return nil
}

func (c *Conn) handleCommand(cmd byte) error {
	switch cmd {
	case cmdDo, cmdDont, cmdWill, cmdWont:
		// Ignore and deny all options.
		opt, err := c.reader.ReadByte()
		if err != nil {
			return err
		}
		return c.handleOption(cmd, opt)
	case cmdGA:
		// Ignore Go-ahead.
	case cmdSB:
		// Skip subnegotiation by reading until the server stops
		// negotiating.
		var seq [2]byte
		for seq[0] != cmdStart && seq[1] != cmdSE {
			b, err := c.reader.ReadByte()
			if err != nil {
				return err
			}
			seq[0] = seq[1]
			seq[1] = b
		}

	default:
		return fmt.Errorf("unknown telnet command: %d", cmd)
	}
	return nil
}

// ReadUntil reads from the connection until the string |s| is seen.
//
// Since vanilla telnet operates purely on ASCII, any non-ASCII inputs
// may block indefinitely.
func (c *Conn) ReadUntil(s string) error {
	var nextIsCmd bool
	var i int
	match := []byte(s)
	for i < len(match) {
		b, err := c.reader.ReadByte()
		if err != nil {
			return err
		}
		switch {
		case nextIsCmd && b != cmdStart:
			if err := c.handleCommand(b); err != nil {
				return err
			}
			nextIsCmd = false
		case !nextIsCmd && b == cmdStart:
			nextIsCmd = true
		default:
			if c.Output != nil {
				fmt.Fprintf(c.Output, "%c", b)
			}
			if b == match[i] {
				i += 1
			} else {
				i = 0
			}
		}
	}
	return nil
}

// Writeln writes the given string to the telnet connection with a telnet newline at
// the end ("\r\n").
//
// Note that vanilla telnet only supports ASCII, so use non-ASCII characters at your
// own risk.
func (c *Conn) Writeln(s string) error {
	buf := []byte(s)
	// If we encounted a cmdStart (\xFF), we need to escape it, because
	// cmdStart is a reserved byte in Telnet. To do this, we simply escape
	// it with itself. Note that QuoteToASCII may actually produce \xFF, so
	// this is still necessary.
	for i := bytes.IndexByte(buf, cmdStart); i != -1; buf = buf[i+1:] {
		// Write what we've iterated over since the last time we
		// had to stop.
		if _, err := c.Conn.Write(buf[:i]); err != nil {
			return err
		}
		// Write the corrected sequence.
		if _, err := c.Conn.Write([]byte{cmdStart, cmdStart}); err != nil {
			return err
		}
	}
	if _, err := c.Conn.Write(buf); err != nil {
		return err
	}
	if _, err := c.Conn.Write([]byte("\r\n")); err != nil {
		return err
	}
	if c.Output != nil {
		fmt.Fprintf(c.Output, "%s\n", s)
	}
	return nil
}
