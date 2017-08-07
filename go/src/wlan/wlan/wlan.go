// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	"apps/netstack/eth"
	mlme "apps/wlan/services/wlan_mlme"
	mlme_ext "apps/wlan/services/wlan_mlme_ext"
	bindings "fidl/bindings"
	"fmt"
	"log"
	"os"
	"syscall"
	"syscall/mx"
	"syscall/mx/mxerror"
)

const MX_SOCKET_READABLE = mx.SignalObject0
const debug = false

type Client struct {
	path     string
	f        *os.File
	wlanChan mx.Channel
	cfg      *Config
	ap       *AP
	txid     uint64

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
	m := syscall.MXIOForFD(int(f.Fd()))
	if m == nil {
		return nil, fmt.Errorf("wlan: no mxio for %s fd: %d", path, f.Fd())
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
		path:     path,
		f:        f,
		wlanChan: mx.Channel{ch},
		cfg:      config,
		state:    nil,
	}
	success = true
	return c, nil
}

func (c *Client) Close() {
	c.f.Close()
	c.wlanChan.Close()
}

func (c *Client) Run() {
	log.Printf("Running wlan for %v", c.path)
	defer c.Close()

	var err error
	c.state = newScanState(c)

event_loop:
	for {
		if err = c.state.run(c); err != nil {
			log.Printf("could not run state \"%v\": %v", c.state, err)
			break
		}

		nextTimeout := c.state.nextTimeout()
		timeout := mx.TimensecInfinite
		if nextTimeout > 0 {
			timeout = mx.Sys_deadline_after(mx.Duration(nextTimeout.Nanoseconds()))
		}
		obs, err := c.wlanChan.Handle.WaitOne(mx.SignalChannelReadable|mx.SignalChannelPeerClosed, timeout)

		var nextState state = nil
		switch mxerror.Status(err) {
		case mx.ErrBadHandle, mx.ErrCanceled, mx.ErrPeerClosed:
			log.Printf("error waiting on handle: %v", err)
			break event_loop

		case mx.ErrTimedOut:
			nextState, err = c.state.handleTimeout(c)
			if err != nil {
				log.Printf("error handling timeout for state \"%v\": %v", c.state, err)
				break event_loop
			}

		case mx.ErrOk:
			switch {
			case obs&mx.SignalChannelPeerClosed != 0:
				log.Println("channel closed")
				break event_loop

			case obs&MX_SOCKET_READABLE != 0:
				// TODO(tkilbourn): decide on a default buffer size, and support growing the buffer as needed
				var buf [4096]byte
				_, _, err := c.wlanChan.Read(buf[:], nil, 0)
				if err != nil {
					log.Printf("error reading from channel: %v", err)
					break event_loop
				}

				if resp, err := parseResponse(buf[:]); err != nil {
					log.Printf("error parsing message for \"%v\": %v", c.state, err)
					break event_loop
				} else {
					nextState, err = c.state.handleMsg(resp, c)
					if err != nil {
						log.Printf("error handling message (%T) for \"%v\": %v", resp, c.state, err)
						break event_loop
					}
				}
			}
		}

		if nextState != c.state {
			log.Printf("%v -> %v", c.state, nextState)
			c.state = nextState
		}
	}

	log.Printf("exiting event loop for %v", c.path)
}

func (c *Client) sendMessage(msg bindings.Payload, ordinal int32) error {
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
	if err := c.wlanChan.Write(msgBuf, nil, 0); err != nil {
		return fmt.Errorf("could not write to wlan channel: %v", err)
	}
	return nil

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
	default:
		return nil, fmt.Errorf("unknown ordinal: %v", header.ordinal)
	}
}
