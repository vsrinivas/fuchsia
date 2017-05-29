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

	time.Sleep(5 * time.Second)
	return nil
}

func (c *Client) doAuthenticate() error {
	if debug {
		log.Printf("doAuthenticate")
	}

	time.Sleep(5 * time.Second)
	return nil
}

func (c *Client) doAssociate() error {
	if debug {
		log.Printf("doAssociate")
	}

	time.Sleep(5 * time.Second)
	return nil
}

func (c *Client) doRun() error {
	if debug {
		log.Printf("doRun")
	}

	time.Sleep(5 * time.Second)
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
	log.Printf("req: %v", req)

	enc := bindings.NewEncoder()
	// Create a method call header, similar to that of FIDL2
	enc.StartStruct(16, 0)
	if err := enc.WriteUint64(1); err != nil {
		return fmt.Errorf("could not encode txid: %v", err)
	}
	if err := enc.WriteUint32(0); err != nil {
		return fmt.Errorf("could not encode flags: %v", err)
	}
	if err := enc.WriteInt32(int32(mlme.Method_ScanRequest)); err != nil {
		return fmt.Errorf("could not encode ordinal: %v", err)
	}
	enc.Finish()
	if err := req.Encode(enc); err != nil {
		return fmt.Errorf("could not encode ScanRequest: %v", err)
	}

	reqBuf, _, encErr := enc.Data()
	if encErr != nil {
		return fmt.Errorf("could not get encoding data: %v", encErr)
	}
	log.Printf("encoded ScanRequest: %v", reqBuf)
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

func CollectResults(resp *mlme.ScanResponse, ssid string) []AP {
	aps := []AP{}
	for _, s := range resp.BssDescriptionSet {
		if s.Ssid == ssid {
			ap := NewAP(s.Bssid, s.Ssid)
			ap.LastRSSI = s.RssiMeasurement
			aps = append(aps, *ap)
		}
	}
	sort.Slice(aps, func(i, j int) bool { return int8(aps[i].LastRSSI) > int8(aps[j].LastRSSI) })
	return aps
}
