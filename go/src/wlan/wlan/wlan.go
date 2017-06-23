// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	"apps/netstack/eth"
	mlme "apps/wlan/services/wlan_mlme"
	bindings "fidl/bindings"
	"fmt"
	"log"
	"os"
	"sort"
	"syscall"
	"syscall/mx"
	"syscall/mx/mxerror"
	"time"
)

const MX_SOCKET_READABLE = mx.SignalObject0
const DefaultScanInterval = 5 * time.Second
const ScanTimeout = 30 * time.Second

const debug = true

type State int

const (
	StateUnknown = State(iota)
	StateStarted
	StateScanning
	StateJoining
	StateAuthenticating
	StateAssociating
	StateAssociated
	StateStopped
)

func (s State) String() string {
	switch s {
	case StateUnknown:
		return "wlan unknown state"
	case StateStarted:
		return "wlan started"
	case StateScanning:
		return "wlan scanning"
	case StateJoining:
		return "wlan joining"
	case StateAuthenticating:
		return "wlan authenticating"
	case StateAssociating:
		return "wlan associating"
	case StateAssociated:
		return "wlan associated"
	case StateStopped:
		return "wlan stopped"
	default:
		return fmt.Sprintf("wlan bad state (%d)", int(s))
	}
}

type Client struct {
	path     string
	f        *os.File
	wlanChan mx.Channel
	cfg      *Config
	state    State
	ap       *AP
	txid     uint64
}

func NewClient(path string, config *Config) (*Client, error) {
	f, err := os.Open(path)
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
		state:    StateStarted,
	}
	return c, nil
}

func (c *Client) Run() {
	log.Printf("Running wlan for %v", c.path)

	for {
		var err error
		switch c.state {
		case StateUnknown:
			err = fmt.Errorf("unknown state; shutting down")
		case StateStarted:
			c.state = StateScanning
		case StateScanning:
			err = c.doScan()
		case StateJoining:
			err = c.doJoin()
		case StateAuthenticating:
			err = c.doAuthenticate()
		case StateAssociating:
			err = c.doAssociate()
		case StateAssociated:
			err = c.doRun()
		case StateStopped:
			err = fmt.Errorf("stopping")
		}

		if err != nil {
			log.Print(err)
			return
		}
	}
}

func (c *Client) doScan() error {
	if debug {
		log.Printf("doScan")
	}

	scanInterval := DefaultScanInterval
	if c.cfg != nil && c.cfg.ScanInterval > 0 {
		scanInterval = time.Duration(c.cfg.ScanInterval) * time.Second
	}

scan_loop:
	for {
		if err := c.requestScan(); err != nil {
			return fmt.Errorf("scan failed: %v", err)
		}

		scanTimeout := mx.Sys_deadline_after(mx.Duration(ScanTimeout.Nanoseconds()))
		obs, err := c.wlanChan.Handle.WaitOne(mx.SignalChannelReadable|mx.SignalChannelPeerClosed, scanTimeout)

		var resp *mlme.ScanResponse
		switch mxerror.Status(err) {
		case mx.ErrBadHandle, mx.ErrCanceled, mx.ErrPeerClosed:
			c.state = StateStopped
			return fmt.Errorf("error waiting on handle: %v", err)
		case mx.ErrTimedOut:
			// TODO(tkilbourn): obviously sleeping here is wrong, but it'll do for now
			time.Sleep(scanInterval)
			goto scan_loop
		case mx.ErrOk:
			switch {
			case obs&mx.SignalChannelPeerClosed != 0:
				c.state = StateStopped
				return fmt.Errorf("channel closed")

			case obs&MX_SOCKET_READABLE != 0:
				// TODO(tkilbourn): decide on a default buffer size, and support growing the buffer as needed
				var buf [4096]byte
				_, _, err := c.wlanChan.Read(buf[:], nil, 0)
				if err != nil {
					c.state = StateStopped
					return fmt.Errorf("error reading from channel: %v", err)
				}

				if resp = parseScanResponse(buf[:]); resp == nil {
					continue scan_loop
				}
			}
		}

		if resp == nil {
			panic("scan response not decoded!")
		}

		if debug {
			PrintScanResponse(resp)
		}

		// If we have a config, try to join if we found an AP with our SSID.
		// TODO(tkilbourn): better client state machine for dealing with config changes
		if c.cfg != nil {
			aps := CollectResults(resp, c.cfg.SSID)
			if len(aps) > 0 {
				c.ap = &aps[0]
				c.state = StateJoining
				return nil
			}
		}

		// TODO(tkilbourn): should not sleep here
		time.Sleep(scanInterval)
	}

}

func (c *Client) doJoin() error {
	if debug {
		log.Printf("doJoin")
	}

	if err := c.requestJoin(); err != nil {
		return fmt.Errorf("join failed: %v", err)
	}

	var resp mlme.JoinResponse
	obs, err := c.wlanChan.Handle.WaitOne(mx.SignalChannelReadable|mx.SignalChannelPeerClosed, mx.TimensecInfinite)

	switch mxerror.Status(err) {
	case mx.ErrBadHandle, mx.ErrCanceled, mx.ErrPeerClosed:
		return fmt.Errorf("error waiting on handle: %v", err)
	case mx.ErrOk:
		switch {
		case obs&mx.SignalChannelPeerClosed != 0:
			c.state = StateStopped
			return fmt.Errorf("channel closed")

		case obs&MX_SOCKET_READABLE != 0:
			var buf [4096]byte
			_, _, err := c.wlanChan.Read(buf[:], nil, 0)
			if err != nil {
				c.state = StateStopped
				return fmt.Errorf("error reading from channel: %v", err)
			}

			dec := bindings.NewDecoder(buf[:], nil)
			var header APIHeader
			if err := header.Decode(dec); err != nil {
				return fmt.Errorf("could not decode api header: %v", err)
			}
			switch header.ordinal {
			case int32(mlme.Method_JoinConfirm):
				if err := resp.Decode(dec); err != nil {
					return fmt.Errorf("could not decode JoinResponse: %v", err)
				}
			default:
				if debug {
					log.Printf("unknown message ordinal: %v", header.ordinal)
				}
				return nil
			}
		}
	}

	if debug {
		PrintJoinResponse(&resp)
	}

	if resp.ResultCode == mlme.JoinResultCodes_Success {
		c.state = StateAuthenticating
	} else {
		c.state = StateScanning
	}

	return nil
}

func (c *Client) doAuthenticate() error {
	if debug {
		log.Printf("doAuthenticate")
	}

	if err := c.requestAuthenticate(); err != nil {
		return fmt.Errorf("authenticate failed: %v", err)
	}

	var resp mlme.AuthenticateResponse
	obs, err := c.wlanChan.Handle.WaitOne(mx.SignalChannelReadable|mx.SignalChannelPeerClosed, mx.TimensecInfinite)

	switch mxerror.Status(err) {
	case mx.ErrBadHandle, mx.ErrCanceled, mx.ErrPeerClosed:
		return fmt.Errorf("error waiting on handle: %v", err)
	case mx.ErrOk:
		switch {
		case obs&mx.SignalChannelPeerClosed != 0:
			c.state = StateStopped
			return fmt.Errorf("channel closed")

		case obs&MX_SOCKET_READABLE != 0:
			var buf [4096]byte
			if _, _, err := c.wlanChan.Read(buf[:], nil, 0); err != nil {
				c.state = StateStopped
				return fmt.Errorf("error reading from channel: %v", err)
			}

			dec := bindings.NewDecoder(buf[:], nil)
			var header APIHeader
			if err := header.Decode(dec); err != nil {
				return fmt.Errorf("could not decode api header: %v", err)
			}
			if header.ordinal == int32(mlme.Method_AuthenticateConfirm) {
				if err := resp.Decode(dec); err != nil {
					return fmt.Errorf("could not decode AuthenticateResponse: %v", err)
				}
			} else {
				if debug {
					log.Printf("unknown message ordinal: %v", header.ordinal)
				}
				return nil
			}
		}
	}

	if debug {
		PrintAuthenticateResponse(&resp)
	}

	if resp.ResultCode == mlme.AuthenticateResultCodes_Success {
		c.state = StateAssociating
	} else {
		c.state = StateScanning
	}

	return nil
}

func (c *Client) doAssociate() error {
	if debug {
		log.Printf("doAssociate")
	}

	if err := c.requestAssociate(); err != nil {
		return fmt.Errorf("associate failed: %v", err)
	}

	var resp mlme.AssociateResponse
	obs, err := c.wlanChan.Handle.WaitOne(mx.SignalChannelReadable|mx.SignalChannelPeerClosed, mx.TimensecInfinite)

	switch mxerror.Status(err) {
	case mx.ErrBadHandle, mx.ErrCanceled, mx.ErrPeerClosed:
		return fmt.Errorf("error waiting on handle: %v", err)
	case mx.ErrOk:
		switch {
		case obs&mx.SignalChannelPeerClosed != 0:
			c.state = StateStopped
			return fmt.Errorf("channel closed")

		case obs&MX_SOCKET_READABLE != 0:
			var buf [4096]byte
			if _, _, err := c.wlanChan.Read(buf[:], nil, 0); err != nil {
				c.state = StateStopped
				return fmt.Errorf("error reading from channel: %v", err)
			}

			dec := bindings.NewDecoder(buf[:], nil)
			var header APIHeader
			if err := header.Decode(dec); err != nil {
				return fmt.Errorf("could not decode api header: %v", err)
			}
			if header.ordinal == int32(mlme.Method_AssociateConfirm) {
				if err := resp.Decode(dec); err != nil {
					return fmt.Errorf("could not decode AssociateResponse: %v", err)
				}
			} else {
				if debug {
					log.Printf("unknown message ordinal: %v", header.ordinal)
				}
				return nil
			}
		}
	}

	if debug {
		PrintAssociateResponse(&resp)
	}

	if resp.ResultCode == mlme.AssociateResultCodes_Success {
		c.state = StateAssociated
	} else {
		c.state = StateScanning
	}

	return nil
}

func (c *Client) doRun() error {
	if debug {
		log.Printf("doRun")
	}

	obs, err := c.wlanChan.Handle.WaitOne(mx.SignalChannelReadable|mx.SignalChannelPeerClosed, mx.TimensecInfinite)

	switch mxerror.Status(err) {
	case mx.ErrBadHandle, mx.ErrCanceled, mx.ErrPeerClosed:
		return fmt.Errorf("error waiting on handle: %v", err)
	case mx.ErrOk:
		switch {
		case obs&mx.SignalChannelPeerClosed != 0:
			c.state = StateStopped
			return fmt.Errorf("channel closed")

		case obs&MX_SOCKET_READABLE != 0:
			var buf [4096]byte
			if _, _, err := c.wlanChan.Read(buf[:], nil, 0); err != nil {
				c.state = StateStopped
				return fmt.Errorf("error reading from channel: %v", err)
			}

			dec := bindings.NewDecoder(buf[:], nil)
			var header APIHeader
			if err := header.Decode(dec); err != nil {
				return fmt.Errorf("could not decode api header: %v", err)
			}
			switch header.ordinal {
			case int32(mlme.Method_DisassociateIndication):
				c.state = StateAssociating
				var ind mlme.DisassociateIndication
				if err := ind.Decode(dec); err != nil {
					// TODO(tkilbourn): wait the appropriate amount of time before attempting to reassociate
					return fmt.Errorf("could not decode DisassociateIndication: %v (disassociating anyway)", err)
				}
				if debug {
					PrintDisassociateIndication(&ind)
				}
			case int32(mlme.Method_DeauthenticateIndication):
				c.state = StateAuthenticating
				var ind mlme.DeauthenticateIndication
				if err := ind.Decode(dec); err != nil {
					// TODO(tkilbourn): wait the appropriate amount of time before attempting to reauthenticate
					return fmt.Errorf("could not decode DeauthenticateIndication: %v (deauthenticating anyway)", err)
				}
				if debug {
					PrintDeauthenticateIndication(&ind)
				}
			default:
				if debug {
					log.Printf("unknown message ordinal: %v", header.ordinal)
				}
			}
		}
	}

	return nil
}

var twoPointFourGhzChannels = []uint16{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}

func (c *Client) requestScan() error {
	req := &mlme.ScanRequest{
		BssType:        mlme.BssTypes_Infrastructure,
		ScanType:       mlme.ScanTypes_Passive,
		ChannelList:    &twoPointFourGhzChannels,
		MinChannelTime: 100,
		MaxChannelTime: 300,
	}
	if c.cfg != nil {
		req.Ssid = c.cfg.SSID
	}
	if debug {
		log.Printf("scan req: %v", req)
	}

	h := &APIHeader{
		txid:    c.nextTxid(),
		flags:   0,
		ordinal: int32(mlme.Method_ScanRequest),
	}

	enc := bindings.NewEncoder()
	if err := h.Encode(enc); err != nil {
		return fmt.Errorf("could not encode header: %v", err)
	}
	if err := req.Encode(enc); err != nil {
		return fmt.Errorf("could not encode ScanRequest: %v", err)
	}

	reqBuf, _, encErr := enc.Data()
	if encErr != nil {
		return fmt.Errorf("could not get encoding data: %v", encErr)
	}
	if debug {
		log.Printf("encoded ScanRequest: %v", reqBuf)
	}
	if err := c.wlanChan.Write(reqBuf, nil, 0); err != nil {
		return fmt.Errorf("could not write to wlan channel: %v", err)
	}
	return nil
}

func (c *Client) requestJoin() error {
	if c.state != StateJoining {
		panic(fmt.Sprintf("invalid state for joining: %v", c.state))
	}

	req := &mlme.JoinRequest{
		SelectedBss:        *c.ap.BSSDesc,
		JoinFailureTimeout: 20,
	}
	if debug {
		log.Printf("join req: %v", req)
	}

	h := &APIHeader{
		txid:    c.nextTxid(),
		flags:   0,
		ordinal: int32(mlme.Method_JoinRequest),
	}

	enc := bindings.NewEncoder()
	if err := h.Encode(enc); err != nil {
		return fmt.Errorf("could not encode header: %v", err)
	}
	if err := req.Encode(enc); err != nil {
		return fmt.Errorf("could not encode JoinRequest: %v", err)
	}

	reqBuf, _, encErr := enc.Data()
	if encErr != nil {
		return fmt.Errorf("could not get encoding data: %v", encErr)
	}
	if debug {
		log.Printf("encoded JoinRequest: %v", reqBuf)
	}
	if err := c.wlanChan.Write(reqBuf, nil, 0); err != nil {
		return fmt.Errorf("could not write to wlan channel: %v", err)
	}
	return nil
}

func parseScanResponse(buf []byte) *mlme.ScanResponse {
	dec := bindings.NewDecoder(buf, nil)
	var header APIHeader
	if err := header.Decode(dec); err != nil {
		if debug {
			log.Printf("could not decode api header: %v", err)
		}
		return nil
	}
	switch header.ordinal {
	case int32(mlme.Method_ScanConfirm):
		var resp mlme.ScanResponse
		if err := resp.Decode(dec); err != nil {
			if debug {
				log.Printf("could not decode ScanResponse: %v", err)
			}
			return nil
		}
		return &resp
	default:
		if debug {
			log.Printf("unknown message ordinal: %v", header.ordinal)
		}
		return nil
	}
}

func (c *Client) requestAuthenticate() error {
	if c.state != StateAuthenticating {
		panic(fmt.Sprintf("invalid state for authenticating: %v", c.state))
	}

	req := &mlme.AuthenticateRequest{
		PeerStaAddress:     c.ap.BSSDesc.Bssid,
		AuthType:           mlme.AuthenticationTypes_OpenSystem,
		AuthFailureTimeout: 20,
	}
	if debug {
		log.Printf("auth req: %v", req)
	}

	h := &APIHeader{
		txid:    c.nextTxid(),
		flags:   0,
		ordinal: int32(mlme.Method_AuthenticateRequest),
	}

	enc := bindings.NewEncoder()
	if err := h.Encode(enc); err != nil {
		return fmt.Errorf("could not encode header: %v", err)
	}
	if err := req.Encode(enc); err != nil {
		return fmt.Errorf("could not encode AuthenticateRequest: %v", err)
	}

	reqBuf, _, encErr := enc.Data()
	if encErr != nil {
		return fmt.Errorf("could not code encoded data: %v", encErr)
	}
	if debug {
		log.Printf("encoded AuthenticateRequest: %v", reqBuf)
	}
	if err := c.wlanChan.Write(reqBuf, nil, 0); err != nil {
		return fmt.Errorf("could not write to wlan channel: %v", err)
	}
	return nil
}

func (c *Client) requestAssociate() error {
	if c.state != StateAssociating {
		panic(fmt.Sprintf("invalid state for associating: %v", c.state))
	}

	req := &mlme.AssociateRequest{
		PeerStaAddress: c.ap.BSSDesc.Bssid,
	}
	if debug {
		log.Printf("assoc req: %v", req)
	}

	h := &APIHeader{
		txid:    c.nextTxid(),
		flags:   0,
		ordinal: int32(mlme.Method_AssociateRequest),
	}

	enc := bindings.NewEncoder()
	if err := h.Encode(enc); err != nil {
		return fmt.Errorf("could not encode header: %v", err)
	}
	if err := req.Encode(enc); err != nil {
		return fmt.Errorf("could not encode AssociateRequest: %v", err)
	}

	reqBuf, _, encErr := enc.Data()
	if encErr != nil {
		return fmt.Errorf("could not get encoded data: %v", encErr)
	}
	if debug {
		log.Printf("encoded AssociateRequest: %v", reqBuf)
	}
	if err := c.wlanChan.Write(reqBuf, nil, 0); err != nil {
		return fmt.Errorf("could not write to wlan channel: %v", err)
	}
	return nil
}

func (c *Client) nextTxid() (txid uint64) {
	txid = c.txid
	c.txid++
	return
}

func CollectResults(resp *mlme.ScanResponse, ssid string) []AP {
	aps := []AP{}
	for _, s := range resp.BssDescriptionSet {
		if s.Ssid == ssid {
			ap := NewAP(&s)
			ap.LastRSSI = s.RssiMeasurement
			aps = append(aps, *ap)
		}
	}
	sort.Slice(aps, func(i, j int) bool { return int8(aps[i].LastRSSI) > int8(aps[j].LastRSSI) })
	return aps
}
