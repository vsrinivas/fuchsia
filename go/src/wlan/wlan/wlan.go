// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	"fidl/bindings"
	"fmt"
	mlme "garnet/public/lib/wlan/fidl/wlan_mlme"
	mlme_ext "garnet/public/lib/wlan/fidl/wlan_mlme_ext"
	"garnet/public/lib/wlan/fidl/wlan_service"
	"log"
	"netstack/link/eth"
	"os"
	"syscall"
	"syscall/zx"
	"syscall/zx/mxerror"
	"time"
	"wlan/eapol"
)

const ZX_SOCKET_READABLE = zx.SignalObject0
const debug = false

type commandRequest struct {
	id    Command
	arg   interface{}
	respC chan *CommandResult
}

type CommandResult struct {
	Resp interface{}
	Err  *wlan_service.Error
}

type mlmeResult struct {
	observed zx.Signals
	err      error
}

type Client struct {
	cmdC  chan *commandRequest
	mlmeC chan *mlmeResult

	path     string
	f        *os.File
	mlmeChan zx.Channel
	cfg      *Config
	ap       *AP
	staAddr  [6]byte
	txid     uint64
	eapolC   *eapol.Client

	state state
}

func NewClient(path string, config *Config) (*Client, error) {
	success := false
	f, err := os.Open(path)
	defer func() {
		if !success {
			f.Close()
		}
	}()
	if err != nil {
		return nil, fmt.Errorf("wlan: client open: %v", err)
	}
	m := syscall.FDIOForFD(int(f.Fd()))
	if m == nil {
		return nil, fmt.Errorf("wlan: no fdio for %s fd: %d", path, f.Fd())
	}
	info, err := eth.IoctlGetInfo(m)
	if err != nil {
		return nil, err
	}

	if info.Features&eth.FeatureWlan == 0 {
		return nil, nil
	}

	log.Printf("found wlan device %q", path)
	ch, err := ioctlGetChannel(m)
	if err != nil {
		return nil, fmt.Errorf("could not get channel: %v", err)
	}
	c := &Client{
		cmdC:     make(chan *commandRequest, 1),
		mlmeC:    make(chan *mlmeResult, 1),
		path:     path,
		f:        f,
		mlmeChan: zx.Channel{ch},
		cfg:      config,
		state:    nil,
		staAddr:  info.MAC,
	}
	success = true
	return c, nil
}

func (c *Client) Close() {
	c.f.Close()
	c.mlmeChan.Close()
}

func (c *Client) PostCommand(cmd Command, arg interface{}, respC chan *CommandResult) {
	c.cmdC <- &commandRequest{cmd, arg, respC}
}

func (c *Client) Run() {
	log.Printf("Running wlan for %v", c.path)
	defer c.Close()

	var err error
	var mlmeTimeout time.Duration
	var timer *time.Timer
	var timerC <-chan time.Time
	var nextState state

	watchingMLME := false
	c.state = newScanState(c)

event_loop:
	for {
		if mlmeTimeout, err = c.state.run(c); err != nil {
			log.Printf("could not run state \"%v\": %v", c.state, err)
			break
		}

		// We will select 3 channels:
		// 1) We always watch mlmeChan, and c.mlmeC will receive a
		//    mlmeResult if mlmeChan has a message to read, is closed,
		//    or the watch is timed out.
		//    TODO: restart the watch if mlmeTimeout is updated
		if !watchingMLME {
			watchingMLME = true
			go func() {
				c.mlmeC <- c.watchMLMEChan(mlmeTimeout)
			}()
		}

		// 2) c.cmdC receives a command. If the state doesn't want
		//    to handle it, c.state.commandIsDisabled() returns true.
		cmdC := c.cmdC
		if c.state.commandIsDisabled() {
			cmdC = nil
		}

		// 3) If c.state.needTimer true, we start the timer.
		//    timerC will block until the timer is expired.
		timerIsNeeded, duration := c.state.needTimer(c)
		if timerIsNeeded {
			if timer != nil {
				timer.Reset(duration)
			} else {
				timer = time.NewTimer(duration)
			}
			timerC = timer.C
		} else {
			timerC = nil
		}

		select {
		case w := <-c.mlmeC:
			watchingMLME = false
			if timerC != nil && !timer.Stop() {
				<-timer.C
			}
			if debug {
				log.Printf("got a wlan response")
			}
			nextState, err = c.handleResponse(w.observed, w.err)
		case r := <-cmdC:
			if timerC != nil && !timer.Stop() {
				<-timer.C
			}
			if debug {
				log.Printf("got a command")
			}
			nextState, err = c.state.handleCommand(r, c)
		case <-timerC:
			nextState, err = c.state.timerExpired(c)
		}
		if err != nil {
			log.Printf("%v", err)
			break event_loop
		}

		if nextState != c.state {
			log.Printf("%v -> %v", c.state, nextState)
			c.state = nextState
		}
	}

	log.Printf("exiting event loop for %v", c.path)
}

func (c *Client) SendMessage(msg bindings.Payload, ordinal int32) error {
	h := &APIHeader{
		txid:    c.nextTxid(),
		flags:   0,
		ordinal: ordinal,
	}

	enc := bindings.NewEncoder()
	if err := h.Encode(enc); err != nil {
		return fmt.Errorf("could not encode header: %v", err)
	}
	if err := msg.Encode(enc); err != nil {
		return fmt.Errorf("could not encode %T: %v", msg, err)
	}

	msgBuf, _, encErr := enc.Data()
	if encErr != nil {
		return fmt.Errorf("could not get encoding data: %v", encErr)
	}
	if debug {
		log.Printf("encoded message: %v", msgBuf)
	}
	if err := c.mlmeChan.Write(msgBuf, nil, 0); err != nil {
		return fmt.Errorf("could not write to wlan channel: %v", err)
	}

	return nil
}

func (c *Client) watchMLMEChan(timeout time.Duration) *mlmeResult {
	deadline := zx.TimensecInfinite
	if timeout > 0 {
		deadline = zx.Sys_deadline_after(
			zx.Duration(timeout.Nanoseconds()))
	}
	obs, err := c.mlmeChan.Handle.WaitOne(
		zx.SignalChannelReadable|zx.SignalChannelPeerClosed,
		deadline)
	return &mlmeResult{obs, err}
}

func (c *Client) handleResponse(obs zx.Signals, err error) (state, error) {
	var nextState state
	switch mxerror.Status(err) {
	case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
		return nil, fmt.Errorf("error waiting on handle: %v", err)

	case zx.ErrTimedOut:
		nextState, err = c.state.handleMLMETimeout(c)
		if err != nil {
			return nil, fmt.Errorf("error handling timeout for state \"%v\": %v", c.state, err)
		}

	case zx.ErrOk:
		switch {
		case obs&zx.SignalChannelPeerClosed != 0:
			return nil, fmt.Errorf("channel closed")

		case obs&ZX_SOCKET_READABLE != 0:
			// TODO(tkilbourn): decide on a default buffer size, and support growing the buffer as needed
			var buf [4096]byte
			_, _, err := c.mlmeChan.Read(buf[:], nil, 0)
			if err != nil {
				return nil, fmt.Errorf("error reading from channel: %v", err)
			}

			if resp, err := parseResponse(buf[:]); err != nil {
				return nil, fmt.Errorf("error parsing message for \"%v\": %v", c.state, err)
			} else {
				nextState, err = c.state.handleMLMEMsg(resp, c)
				if err != nil {
					return nil, fmt.Errorf("error handling message (%T) for \"%v\": %v", resp, c.state, err)
				}
			}
		}
	default:
		return nil, fmt.Errorf("unknown error: %v", err)
	}
	return nextState, nil
}

func (c *Client) nextTxid() (txid uint64) {
	txid = c.txid
	c.txid++
	return
}

func parseResponse(buf []byte) (interface{}, error) {
	dec := bindings.NewDecoder(buf, nil)
	var header APIHeader
	if err := header.Decode(dec); err != nil {
		return nil, fmt.Errorf("could not decode api header: %v", err)
	}
	switch header.ordinal {
	case int32(mlme.Method_ScanConfirm):
		var resp mlme.ScanResponse
		if err := resp.Decode(dec); err != nil {
			return nil, fmt.Errorf("could not decode ScanResponse: %v", err)
		}
		return &resp, nil
	case int32(mlme.Method_JoinConfirm):
		var resp mlme.JoinResponse
		if err := resp.Decode(dec); err != nil {
			return nil, fmt.Errorf("could not decode JoinResponse: %v", err)
		}
		return &resp, nil
	case int32(mlme.Method_AuthenticateConfirm):
		var resp mlme.AuthenticateResponse
		if err := resp.Decode(dec); err != nil {
			return nil, fmt.Errorf("could not decode AuthenticateResponse: %v", err)
		}
		return &resp, nil
	case int32(mlme.Method_DeauthenticateIndication):
		var ind mlme.DeauthenticateIndication
		if err := ind.Decode(dec); err != nil {
			return nil, fmt.Errorf("could not decode DeauthenticateIndication: %v", err)
		}
		return &ind, nil
	case int32(mlme.Method_AssociateConfirm):
		var resp mlme.AssociateResponse
		if err := resp.Decode(dec); err != nil {
			return nil, fmt.Errorf("could not decode AssociateResponse: %v", err)
		}
		return &resp, nil
	case int32(mlme.Method_DisassociateIndication):
		var ind mlme.DisassociateIndication
		if err := ind.Decode(dec); err != nil {
			return nil, fmt.Errorf("could not decode DisassociateIndication: %v", err)
		}
		return &ind, nil
	case int32(mlme.Method_SignalReportIndication):
		var ind mlme_ext.SignalReportIndication
		if err := ind.Decode(dec); err != nil {
			return nil, fmt.Errorf("could not decode SignalReportIndication: %v", err)
		}
		return &ind, nil
	case int32(mlme.Method_EapolIndication):
		var ind mlme_ext.EapolIndication
		if err := ind.Decode(dec); err != nil {
			return nil, fmt.Errorf("could not decode EapolIndication: %v", err)
		}
		return &ind, nil
	case int32(mlme.Method_EapolConfirm):
		var resp mlme.EapolResponse
		return &resp, nil
	default:
		return nil, fmt.Errorf("unknown ordinal: %v", header.ordinal)
	}
}
