// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	mlme "apps/wlan/services/wlan_mlme"
	mlme_ext "apps/wlan/services/wlan_mlme_ext"

	"fmt"
	"log"
	"time"
)

type state interface {
	run(*Client) error
	handleMsg(interface{}, *Client) (state, error)
	handleTimeout(*Client) (state, error)
	nextTimeout() time.Duration
}

// Scanning

const DefaultScanInterval = 5 * time.Second
const ScanTimeout = 30 * time.Second

type scanState struct {
	scanInterval time.Duration
	pause        bool
}

var twoPointFourGhzChannels = []uint16{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}
var broadcastBssid = [6]uint8{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

func newScanState(c *Client) *scanState {
	scanInterval := DefaultScanInterval
	if c.cfg != nil && c.cfg.ScanInterval > 0 {
		scanInterval = time.Duration(c.cfg.ScanInterval) * time.Second
	}

	return &scanState{scanInterval, false}
}

func (s *scanState) String() string {
	return "scanning"
}

func (s *scanState) run(c *Client) error {
	if !s.pause {
		req := &mlme.ScanRequest{
			BssType:        mlme.BssTypes_Infrastructure,
			Bssid:          broadcastBssid,
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

		return c.sendMessage(req, int32(mlme.Method_ScanRequest))
	}
	return nil
}

func (s *scanState) handleMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.ScanResponse:
		if debug {
			PrintScanResponse(v)
		}

		if c.cfg != nil {
			aps := CollectScanResults(v, c.cfg.SSID)
			if len(aps) > 0 {
				c.ap = &aps[0]
				return newJoinState(), nil
			}
		}
		s.pause = true
		return s, nil

	default:
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *scanState) handleTimeout(c *Client) (state, error) {
	if debug {
		log.Println("scan timeout")
	}
	s.pause = !s.pause
	return s, nil
}

func (s *scanState) nextTimeout() time.Duration {
	if s.pause {
		return s.scanInterval
	} else {
		return ScanTimeout
	}
}

// Joining

type joinState struct {
}

func newJoinState() *joinState {
	return &joinState{}
}

func (s *joinState) String() string {
	return "joining"
}

func (s *joinState) run(c *Client) error {
	req := &mlme.JoinRequest{
		SelectedBss:        *c.ap.BSSDesc,
		JoinFailureTimeout: 20,
	}
	if debug {
		log.Printf("join req: %v", req)
	}

	return c.sendMessage(req, int32(mlme.Method_JoinRequest))
}

func (s *joinState) handleMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.JoinResponse:
		if debug {
			PrintJoinResponse(v)
		}

		if v.ResultCode == mlme.JoinResultCodes_Success {
			return newAuthState(), nil
		} else {
			return newScanState(c), nil
		}
	default:
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *joinState) handleTimeout(c *Client) (state, error) {
	return s, nil
}

func (s *joinState) nextTimeout() time.Duration {
	return 0
}

// Authenticating

type authState struct {
}

func newAuthState() *authState {
	return &authState{}
}

func (s *authState) String() string {
	return "authenticating"
}

func (s *authState) run(c *Client) error {
	req := &mlme.AuthenticateRequest{
		PeerStaAddress:     c.ap.BSSDesc.Bssid,
		AuthType:           mlme.AuthenticationTypes_OpenSystem,
		AuthFailureTimeout: 20,
	}
	if debug {
		log.Printf("auth req: %v", req)
	}

	return c.sendMessage(req, int32(mlme.Method_AuthenticateRequest))
}

func (s *authState) handleMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.AuthenticateResponse:
		if debug {
			PrintAuthenticateResponse(v)
		}

		if v.ResultCode == mlme.AuthenticateResultCodes_Success {
			return newAssocState(), nil
		} else {
			return newScanState(c), nil
		}
	default:
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *authState) handleTimeout(c *Client) (state, error) {
	return s, nil
}

func (s *authState) nextTimeout() time.Duration {
	return 0
}

// Associating

type assocState struct {
}

func newAssocState() *assocState {
	return &assocState{}
}

func (s *assocState) String() string {
	return "associating"
}

func (s *assocState) run(c *Client) error {
	req := &mlme.AssociateRequest{
		PeerStaAddress: c.ap.BSSDesc.Bssid,
	}
	if debug {
		log.Printf("assoc req: %v", req)
	}

	return c.sendMessage(req, int32(mlme.Method_AssociateRequest))
}

func (s *assocState) handleMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.AssociateResponse:
		if debug {
			PrintAssociateResponse(v)
		}

		if v.ResultCode == mlme.AssociateResultCodes_Success {
			return newAssociatedState(), nil
		} else {
			return newScanState(c), nil
		}
	default:
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *assocState) handleTimeout(c *Client) (state, error) {
	return s, nil
}

func (s *assocState) nextTimeout() time.Duration {
	return 0
}

// Associated

type associatedState struct {
}

func newAssociatedState() *associatedState {
	return &associatedState{}
}

func (s *associatedState) String() string {
	return "associated"
}

func (s *associatedState) run(c *Client) error {
	return nil
}

func (s *associatedState) handleMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.DisassociateIndication:
		if debug {
			PrintDisassociateIndication(v)
		}
		return newAssocState(), nil
	case *mlme.DeauthenticateIndication:
		if debug {
			PrintDeauthenticateIndication(v)
		}
		return newAuthState(), nil
	case *mlme_ext.SignalReportIndication:
		if debug {
			PrintSignalReportIndication(v)
		}
		return s, nil
	default:
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *associatedState) handleTimeout(c *Client) (state, error) {
	return s, nil
}

func (s *associatedState) nextTimeout() time.Duration {
	return 0
}
