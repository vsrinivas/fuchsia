// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	bindings "fidl/bindings"
	"fidl/fuchsia/wlan/mlme"
	wlan_service "fidl/fuchsia/wlan/service"
	"fmt"
	"log"
	"os"
	"syscall"
	"syscall/zx"
	"syscall/zx/mxerror"
	"syscall/zx/zxwait"
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
	eapolC   *eapol.Client
	wlanInfo *mlme.DeviceQueryConfirm

	state            state
	pendingStatsResp chan *CommandResult
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
	is_compatible := ap.IsCompatible
	return wlan_service.Ap{bssid, ap.SSID, ap.RssiDbm, is_secure, is_compatible, ap.Chan}
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

func (c *Client) handleQueryStatsCommand(req *commandRequest) {
	err := c.SendMessage(&mlme.MlmeStatsQueryReqRequest{}, mlme.MlmeStatsQueryReqOrdinal)
	if err != nil {
		req.respC <- &CommandResult{nil,
			&wlan_service.Error{wlan_service.ErrCodeInternal, "Could not send MLME request"}}
		return
	}
	c.pendingStatsResp = req.respC
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
			if r.id == CmdStats {
				c.handleQueryStatsCommand(r)
			} else {
				nextState, err = c.state.handleCommand(r, c)
			}
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
	// All MLME messages are one-way, so the txid is 0.
	h := &bindings.MessageHeader{
		Txid:    0,
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
	obs, err := zxwait.Wait(*c.mlmeChan.Handle(),
		zx.SignalChannelReadable|zx.SignalChannelPeerClosed,
		deadline)
	return &mlmeResult{obs, err}
}

func (c *Client) handleMLMEMsg(resp interface{}) (state, error) {
	nextState := c.state
	switch resp.(type) {
	// Query Stats is state-independent so handle it immediately.
	case *mlme.StatsQueryResponse:
		if c.pendingStatsResp != nil {
			c.pendingStatsResp <- &CommandResult{ resp.(*mlme.StatsQueryResponse).Stats, nil }
			c.pendingStatsResp = nil
		}
	// Everything else is state-dependent
	default:
		var err error
		nextState, err = c.state.handleMLMEMsg(resp, c)
		if err != nil {
			return nil, fmt.Errorf("error handling message (%T) for \"%v\": %v", resp, c.state, err)
		}
	}
	return nextState, nil
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
			var buf [16384]byte
			_, _, err := c.mlmeChan.Read(buf[:], nil, 0)
			if err != nil {
				return nil, fmt.Errorf("error reading from channel: %v", err)
			}

			if resp, err := parseResponse(buf[:]); err != nil {
				return nil, fmt.Errorf("error parsing message for \"%v\": %v", c.state, err)
			} else {
				nextState, err = c.handleMLMEMsg(resp)
			}
		}
	default:
		return nil, fmt.Errorf("unknown error: %v", err)
	}
	return nextState, nil
}

func parseResponse(buf []byte) (interface{}, error) {
	var header bindings.MessageHeader
	if err := bindings.UnmarshalHeader(buf, &header); err != nil {
		return nil, fmt.Errorf("could not decode api header: %v", err)
	}
	buf = buf[bindings.MessageHeaderSize:]
	switch header.Ordinal {
	case mlme.MlmeScanConfOrdinal:
		var resp mlme.ScanConfirm
		if err := bindings.Unmarshal(buf, nil, &resp); err != nil {
			return nil, fmt.Errorf("could not decode ScanConfirm: %v", err)
		}
		return &resp, nil
	case mlme.MlmeJoinConfOrdinal:
		var resp mlme.JoinConfirm
		if err := bindings.Unmarshal(buf, nil, &resp); err != nil {
			return nil, fmt.Errorf("could not decode JoinConfirm: %v", err)
		}
		return &resp, nil
	case mlme.MlmeAuthenticateConfOrdinal:
		var resp mlme.AuthenticateConfirm
		if err := bindings.Unmarshal(buf, nil, &resp); err != nil {
			return nil, fmt.Errorf("could not decode AuthenticateConfirm: %v", err)
		}
		return &resp, nil
	case mlme.MlmeDeauthenticateConfOrdinal:
		var resp mlme.DeauthenticateConfirm
		if err := bindings.Unmarshal(buf, nil, &resp); err != nil {
			return nil, fmt.Errorf("could not decode DeauthenticateConfirm: %v", err)
		}
		return &resp, nil
	case mlme.MlmeDeauthenticateIndOrdinal:
		var ind mlme.DeauthenticateIndication
		if err := bindings.Unmarshal(buf, nil, &ind); err != nil {
			return nil, fmt.Errorf("could not decode DeauthenticateIndication: %v", err)
		}
		return &ind, nil
	case mlme.MlmeAssociateConfOrdinal:
		var resp mlme.AssociateConfirm
		if err := bindings.Unmarshal(buf, nil, &resp); err != nil {
			return nil, fmt.Errorf("could not decode AssociateConfirm: %v", err)
		}
		return &resp, nil
	case mlme.MlmeDisassociateIndOrdinal:
		var ind mlme.DisassociateIndication
		if err := bindings.Unmarshal(buf, nil, &ind); err != nil {
			return nil, fmt.Errorf("could not decode DisassociateIndication: %v", err)
		}
		return &ind, nil
	case mlme.MlmeSignalReportOrdinal:
		var ind mlme.SignalReportIndication
		if err := bindings.Unmarshal(buf, nil, &ind); err != nil {
			return nil, fmt.Errorf("could not decode SignalReportIndication: %v", err)
		}
		return &ind, nil
	case mlme.MlmeEapolIndOrdinal:
		var ind mlme.EapolIndication
		if err := bindings.Unmarshal(buf, nil, &ind); err != nil {
			return nil, fmt.Errorf("could not decode EapolIndication: %v", err)
		}
		return &ind, nil
	case mlme.MlmeEapolConfOrdinal:
		var resp mlme.EapolConfirm
		return &resp, nil
	case mlme.MlmeDeviceQueryConfOrdinal:
		var resp mlme.DeviceQueryConfirm
		if err := bindings.Unmarshal(buf, nil, &resp); err != nil {
			return nil, fmt.Errorf("could not decode DeviceQueryConfirm: %v", err)
		}
		return &resp, nil
	case mlme.MlmeStatsQueryRespOrdinal:
		var resp mlme.StatsQueryResponse
		if err := bindings.Unmarshal(buf, nil, &resp); err != nil {
			return nil, fmt.Errorf("could not decode StatsQueryResponse: %v", err)
		}
		return &resp, nil
	default:
		return nil, fmt.Errorf("unknown ordinal: %v", header.Ordinal)
	}
}
