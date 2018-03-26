// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	bindings "fidl/bindings2"
	"fmt"
	mlme "fuchsia/go/wlan_mlme"
	"fuchsia/go/wlan_service"
	"log"
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
	apCfg    *APConfig
	ap       *AP
	staAddr  [6]uint8
	txid     uint32
	eapolC   *eapol.Client
	wlanInfo *mlme.DeviceQueryResponse

	state state
}

func NewClient(path string, config *Config, apConfig *APConfig) (*Client, error) {
	success := false
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("wlan: client open: %v", err)
	}
	defer func() {
		if !success {
			f.Close()
		}
	}()
	m := syscall.FDIOForFD(int(f.Fd()))
	if m == nil {
		return nil, fmt.Errorf("wlan: no fdio for %s fd: %d", path, f.Fd())
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
		mlmeChan: zx.Channel(ch),
		cfg:      config,
		apCfg:    apConfig,
		state:    nil,
	}
	success = true
	return c, nil
}

func (c *Client) Close() {
	c.f.Close()
	c.mlmeChan.Close()
}

func ConvertWapToAp(ap AP) wlan_service.Ap {
	bssid := make([]uint8, len(ap.BSSID))
	copy(bssid, ap.BSSID[:])
	// Currently we indicate the AP is secure if it supports RSN.
	// TODO: Check if AP supports other types of security mechanism (e.g. WEP)
	is_secure := ap.BSSDesc.Rsn != nil
	// TODO: Revisit this RSSI conversion.
	last_rssi := int8(ap.LastRSSI)
	return wlan_service.Ap{bssid, ap.SSID, int32(last_rssi), is_secure}
}

func (c *Client) Status() wlan_service.WlanStatus {
	var state = wlan_service.StateUnknown

	switch c.state.(type) {
	case *startBSSState:
		state = wlan_service.StateBss
	case *queryState:
		state = wlan_service.StateQuerying
	case *scanState:
		state = wlan_service.StateScanning
	case *joinState:
		state = wlan_service.StateJoining
	case *authState:
		state = wlan_service.StateAuthenticating
	case *assocState:
		state = wlan_service.StateAssociating
	case *associatedState:
		state = wlan_service.StateAssociated
	default:
		state = wlan_service.StateUnknown
	}

	var current_ap *wlan_service.Ap = nil

	if c.ap != nil &&
		state != wlan_service.StateScanning &&
		state != wlan_service.StateBss &&
		state != wlan_service.StateQuerying {
		ap := ConvertWapToAp(*c.ap)
		current_ap = &ap
	}

	return wlan_service.WlanStatus{
		wlan_service.Error{wlan_service.ErrCodeOk, "OK"},
		state,
		current_ap,
	}
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
	c.state = newQueryState()

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

func (c *Client) SendMessage(msg bindings.Payload, ordinal uint32) error {
	h := &bindings.MessageHeader{
		Txid:    c.nextTxid(),
		Flags:   0,
		Ordinal: ordinal,
	}

	msgBuf := make([]byte, zx.ChannelMaxMessageBytes)
	nb, _, err := bindings.MarshalMessage(h, msg, msgBuf, nil)
	if err != nil {
		return fmt.Errorf("could not encode message %T: %v", msg, err)
	}
	msgBuf = msgBuf[:nb]
	if debug {
		log.Printf("encoded message (%v bytes): %v", nb, msgBuf)
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
	obs, err := c.mlmeChan.Handle().WaitOne(
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

func (c *Client) nextTxid() (txid uint32) {
	txid = c.txid
	c.txid++
	return
}

func parseResponse(buf []byte) (interface{}, error) {
	var header bindings.MessageHeader
	if err := bindings.UnmarshalHeader(buf, &header); err != nil {
		return nil, fmt.Errorf("could not decode api header: %v", err)
	}
	buf = buf[bindings.MessageHeaderSize:]
	switch header.Ordinal {
	case uint32(mlme.MethodScanConfirm):
		var resp mlme.ScanResponse
		if err := bindings.Unmarshal(buf, nil, &resp); err != nil {
			return nil, fmt.Errorf("could not decode ScanResponse: %v", err)
		}
		return &resp, nil
	case uint32(mlme.MethodJoinConfirm):
		var resp mlme.JoinResponse
		if err := bindings.Unmarshal(buf, nil, &resp); err != nil {
			return nil, fmt.Errorf("could not decode JoinResponse: %v", err)
		}
		return &resp, nil
	case uint32(mlme.MethodAuthenticateConfirm):
		var resp mlme.AuthenticateResponse
		if err := bindings.Unmarshal(buf, nil, &resp); err != nil {
			return nil, fmt.Errorf("could not decode AuthenticateResponse: %v", err)
		}
		return &resp, nil
	case uint32(mlme.MethodDeauthenticateConfirm):
		var resp mlme.DeauthenticateResponse
		if err := bindings.Unmarshal(buf, nil, &resp); err != nil {
			return nil, fmt.Errorf("could not decode DeauthenticateResponse: %v", err)
		}
		return &resp, nil
	case uint32(mlme.MethodDeauthenticateIndication):
		var ind mlme.DeauthenticateIndication
		if err := bindings.Unmarshal(buf, nil, &ind); err != nil {
			return nil, fmt.Errorf("could not decode DeauthenticateIndication: %v", err)
		}
		return &ind, nil
	case uint32(mlme.MethodAssociateConfirm):
		var resp mlme.AssociateResponse
		if err := bindings.Unmarshal(buf, nil, &resp); err != nil {
			return nil, fmt.Errorf("could not decode AssociateResponse: %v", err)
		}
		return &resp, nil
	case uint32(mlme.MethodDisassociateIndication):
		var ind mlme.DisassociateIndication
		if err := bindings.Unmarshal(buf, nil, &ind); err != nil {
			return nil, fmt.Errorf("could not decode DisassociateIndication: %v", err)
		}
		return &ind, nil
	case uint32(mlme.MethodSignalReportIndication):
		var ind mlme.SignalReportIndication
		if err := bindings.Unmarshal(buf, nil, &ind); err != nil {
			return nil, fmt.Errorf("could not decode SignalReportIndication: %v", err)
		}
		return &ind, nil
	case uint32(mlme.MethodEapolIndication):
		var ind mlme.EapolIndication
		if err := bindings.Unmarshal(buf, nil, &ind); err != nil {
			return nil, fmt.Errorf("could not decode EapolIndication: %v", err)
		}
		return &ind, nil
	case uint32(mlme.MethodEapolConfirm):
		var resp mlme.EapolResponse
		return &resp, nil
	case uint32(mlme.MethodDeviceQueryConfirm):
		var resp mlme.DeviceQueryResponse
		if err := bindings.Unmarshal(buf, nil, &resp); err != nil {
			return nil, fmt.Errorf("could not decode DeviceQueryResponse: %v", err)
		}
		return &resp, nil
	default:
		return nil, fmt.Errorf("unknown ordinal: %v", header.Ordinal)
	}
}
