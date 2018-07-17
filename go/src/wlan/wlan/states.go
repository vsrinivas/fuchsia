// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	"fidl/fuchsia/wlan/mlme"
	wlan_service "fidl/fuchsia/wlan/service"
	"wlan/eapol"
	"wlan/eapol/handshake"
	"wlan/wlan/elements"

	"fmt"
	"log"
	"sort"
	"time"
)

type state interface {
	run(*Client) (time.Duration, error)
	commandIsDisabled() bool
	handleCommand(*commandRequest, *Client) (state, error)
	handleMLMEMsg(interface{}, *Client) (state, error)
	handleMLMETimeout(*Client) (state, error)
	needTimer(*Client) (bool, time.Duration)
	timerExpired(*Client) (state, error)
}

type Command int

const (
	CmdScan Command = iota
	CmdSetScanConfig
	CmdDisconnect
	CmdStartBSS
	CmdStopBSS
	CmdStats
)

const InfiniteTimeout = 0 * time.Second

// Start BSS

const StartBSSTimeout = 30 * time.Second

type startBSSState struct {
	running bool
}

func newStartBSSState(c *Client) *startBSSState {
	return &startBSSState{}
}

func (s *startBSSState) String() string {
	return "starting-bss"
}

func newStartBSSRequest(ssid string, beaconPeriod uint32, dtimPeriod uint32, channel uint8) *mlme.StartRequest {
	return &mlme.StartRequest{
		Ssid:         ssid,
		BeaconPeriod: beaconPeriod,
		DtimPeriod:   dtimPeriod,
		BssType:      mlme.BssTypesInfrastructure,
		Channel:      channel,
	}
}

func newStopBSSRequest(ssid string) *mlme.StopRequest {
	return &mlme.StopRequest{
		Ssid: ssid,
	}
}

func newResetRequest(staAddr [6]uint8) *mlme.ResetRequest {
	return &mlme.ResetRequest{
		StaAddress: staAddr,
	}
}

func (s *startBSSState) run(c *Client) (time.Duration, error) {
	req := newStartBSSRequest(c.apCfg.SSID, uint32(c.apCfg.BeaconPeriod), uint32(c.apCfg.DTIMPeriod), c.apCfg.Channel)
	timeout := StartBSSTimeout
	if req != nil {
		if debug {
			log.Printf("start bss req: %v timeout: %v", req, timeout)
		}
		err := c.SendMessage(req, mlme.MlmeStartReqOrdinal)
		if err != nil {
			return 0, err
		}
		s.running = true
	}
	return timeout, nil
}

func (s *startBSSState) commandIsDisabled() bool {
	return false
}

func (s *startBSSState) handleCommand(cmd *commandRequest, c *Client) (state, error) {
	switch cmd.id {
	case CmdStopBSS:
		res := &CommandResult{}
		req := newStopBSSRequest(c.apCfg.SSID)
		if req != nil {
			if debug {
				log.Printf("stop bss req: %v", req)
			}
			err := c.SendMessage(req, mlme.MlmeStopReqOrdinal)
			if err != nil {
				res.Err = &wlan_service.Error{wlan_service.ErrCodeInternal, "Could not send MLME request"}
			} else {
				// Send MLME-RESET.request to reset and allow MLME to move into Client mode.
				req := newResetRequest(c.staAddr)
				if req != nil {
					if debug {
						log.Printf("reset req: %v", req)
					}
					err := c.SendMessage(req, mlme.MlmeResetReqOrdinal)
					if err != nil {
						res.Err = &wlan_service.Error{wlan_service.ErrCodeInternal, "Could not send MLME request"}
					}
				}
			}
		}

		c.cfg = nil
		c.apCfg = nil
		cmd.respC <- res
		if res.Err == nil {
			return newScanState(c), nil
		}
	default:
		cmd.respC <- &CommandResult{nil,
			&wlan_service.Error{wlan_service.ErrCodeNotSupported,
				"Can't run the command in scanState"}}
	}
	return s, nil
}

func (s *startBSSState) handleMLMEMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.StartConfirm:
		if debug {
			// TODO(hahnr): Print response.
		}
		s.running = false

		// TODO(hahnr): Evaluate response.
		return s, nil

	default:
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *startBSSState) handleMLMETimeout(c *Client) (state, error) {
	if debug {
		log.Printf("start bss timeout")
	}
	return s, nil
}

func (s *startBSSState) needTimer(c *Client) (bool, time.Duration) {
	return false, 0
}

func (s *startBSSState) timerExpired(c *Client) (state, error) {
	return s, nil
}

// Querying

type queryState struct {
}

func newQueryState() *queryState {
	return &queryState{}
}

func (s *queryState) String() string {
	return "querying"
}

func (s *queryState) run(c *Client) (time.Duration, error) {
	req := &mlme.DeviceQueryRequest{}
	if debug {
		log.Printf("query req: %v", req)
	}

	return InfiniteTimeout, c.SendMessage(req, mlme.MlmeDeviceQueryReqOrdinal)
}

func (s *queryState) commandIsDisabled() bool {
	return true
}

func (s *queryState) handleCommand(cmd *commandRequest, c *Client) (state, error) {
	return s, nil
}

func (s *queryState) handleMLMEMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.DeviceQueryConfirm:
		if debug {
			PrintDeviceQueryConfirm(v)
		}
		c.wlanInfo, _ = msg.(*mlme.DeviceQueryConfirm)
		c.staAddr = c.wlanInfo.MacAddr

		// Enter AP mode if ap config was supplied and is active. Else fall back to client mode.
		// TODO(tkilbourn): confirm that the device capabilities include the desired mode
		if c.apCfg != nil && c.apCfg.Active {
			return newStartBSSState(c), nil
		} else {
			return newScanState(c), nil
		}
	default:
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *queryState) handleMLMETimeout(c *Client) (state, error) {
	return s, nil
}

func (s *queryState) needTimer(c *Client) (bool, time.Duration) {
	return false, 0
}

func (s *queryState) timerExpired(c *Client) (state, error) {
	return s, nil
}

// Scanning

const DefaultScanInterval = 5 * time.Second
const ScanTimeout = 30 * time.Second
const ScanSlice = 1

type scanState struct {
	pause             bool
	running           bool
	completed         bool
	cmdPending        *commandRequest
	supportedChannels []uint8
	channelScanOffset int
	aps               []AP
}

var broadcastBssid = [6]uint8{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

func newScanState(c *Client) *scanState {
	pause := true
	if c.cfg != nil && c.cfg.SSID != "" {
		// start periodic scan.
		pause = false
	}
	return &scanState{pause: pause,
		supportedChannels: getSupportedChannels(c),
		channelScanOffset: 0}
}

func (s *scanState) String() string {
	return "scanning"
}

func getSupportedChannels(c *Client) []uint8 {
	// TODO(tkilbourn): unify all our channel representations.
	// TODO(tkilbourn): filter out the supported channels to a reasonable subset for now
	channels := []uint8{}
	for _, band := range c.wlanInfo.Bands {
		for _, ch := range band.Channels {
			if _, ok := supportedChannelMap[ch]; ok {
				channels = append(channels, ch)
			}
		}
	}
	return channels
}

func newScanRequest(ssid string, c *Client, channels []uint8) *mlme.ScanRequest {
	if len(channels) == 0 {
		return nil
	}
	return &mlme.ScanRequest{
		BssType:        mlme.BssTypesInfrastructure,
		Bssid:          broadcastBssid,
		Ssid:           ssid,
		ScanType:       mlme.ScanTypesPassive,
		ChannelList:    &channels,
		MinChannelTime: 100,
		MaxChannelTime: 300,
	}
}

func (s *scanState) getChannelsSlice() []uint8 {
	var length = ScanSlice
	if s.channelScanOffset+ScanSlice > len(s.supportedChannels) {
		length = len(s.supportedChannels) - s.channelScanOffset
	}
	channels := make([]uint8, length)
	channels = s.supportedChannels[s.channelScanOffset : s.channelScanOffset+length]
	if debug {
		log.Printf("Channel Slice: %v Offset: %v Slice: %v Total: %v", channels, s.channelScanOffset, length, len(s.supportedChannels))
	}

	s.channelScanOffset += length
	s.completed = s.channelScanOffset >= len(s.supportedChannels)
	return channels
}

func (s *scanState) run(c *Client) (time.Duration, error) {
	var req *mlme.ScanRequest
	timeout := ScanTimeout
	if s.cmdPending != nil && s.cmdPending.id == CmdScan {
		sr := s.cmdPending.arg.(*wlan_service.ScanRequest)
		if sr.Timeout > 0 {
			timeout = time.Duration(sr.Timeout) * time.Second
		}
		req = newScanRequest("", c, s.getChannelsSlice())
	} else if c.cfg != nil && c.cfg.SSID != "" && !s.pause {
		req = newScanRequest(c.cfg.SSID, c, s.getChannelsSlice())
	}
	if req != nil {
		if debug {
			log.Printf("scan req: %v timeout: %v", req, timeout)
		}
		err := c.SendMessage(req, mlme.MlmeStartScanOrdinal)
		if err != nil {
			return 0, err
		}
		s.running = true
	}
	return timeout, nil
}

func (s *scanState) commandIsDisabled() bool {
	return s.running
}

func (s *scanState) handleCommand(cmd *commandRequest, c *Client) (state, error) {
	switch cmd.id {
	case CmdScan:
		_, ok := cmd.arg.(*wlan_service.ScanRequest)
		if !ok {
			res := &CommandResult{}
			res.Err = &wlan_service.Error{
				wlan_service.ErrCodeInvalidArgs,
				"Invalid arguments",
			}
			cmd.respC <- res
		}
		s.cmdPending = cmd
	case CmdSetScanConfig:
		newCfg, ok := cmd.arg.(*Config)
		res := &CommandResult{}
		if !ok {
			res.Err = &wlan_service.Error{
				wlan_service.ErrCodeInvalidArgs,
				"Invalid arguments",
			}
		} else {
			c.cfg = newCfg
			if c.cfg != nil && c.cfg.SSID != "" {
				s.pause = false
			}
			if debug {
				log.Printf("New cfg: SSID %v, interval %v",
					c.cfg.SSID, c.cfg.ScanInterval)
			}
		}
		cmd.respC <- res
	case CmdStartBSS:
		newCfg, ok := cmd.arg.(*APConfig)
		c.cfg = nil
		c.apCfg = newCfg

		res := &CommandResult{}
		if !ok {
			res.Err = &wlan_service.Error{
				wlan_service.ErrCodeInvalidArgs,
				"Invalid arguments",
			}
		} else {
			// Send MLME-RESET.request to reset and allow MLME to move into AP mode.
			req := newResetRequest(c.staAddr)
			if req != nil {
				if debug {
					log.Printf("reset req: %v", req)
				}
				err := c.SendMessage(req, mlme.MlmeResetReqOrdinal)
				if err != nil {
					res.Err = &wlan_service.Error{wlan_service.ErrCodeInternal, "Could not send MLME request"}
				}
			}
		}
		cmd.respC <- res
		if res.Err == nil {
			return newStartBSSState(c), nil
		}
	default:
		cmd.respC <- &CommandResult{nil,
			&wlan_service.Error{wlan_service.ErrCodeNotSupported,
				"Can't run the command in scanState"}}
	}
	return s, nil
}

// TODO(NET-420) The logic to pick the optimal AP should be moved into policy
func sortAndDedupeAPs(aps []AP) []AP {
	var aps_map = make(map[string]AP)
	for _, ap := range aps[:] {
		if val, ok := aps_map[ap.SSID]; ok {
			if val.RssiDbm > ap.RssiDbm {
				aps_map[ap.SSID] = ap
			}
		} else {
			aps_map[ap.SSID] = ap
		}
	}

	var sorted_aps []AP
	for _, value := range aps_map {
		sorted_aps = append(sorted_aps, value)
	}
	sort.Sort(ByRSSI(sorted_aps))
	return sorted_aps
}

func (s *scanState) handleMLMEMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.ScanConfirm:
		if debug {
			PrintScanConfirm(v)
		}
		s.running = !s.completed

		if s.cmdPending != nil && s.cmdPending.id == CmdScan {
			aps := CollectScanResults(v, "", "")
			s.aps = append(s.aps, aps...)
			// TODO(NET-420) find a way to stream results upward and let higher level
			// take care of de-duping
			if !s.running {
				var sorted_aps = sortAndDedupeAPs(s.aps)
				s.cmdPending.respC <- &CommandResult{sorted_aps, nil}
				s.channelScanOffset = 0
				s.aps = s.aps[:0]
				s.cmdPending = nil
			}
		} else if c.cfg != nil && c.cfg.SSID != "" {
			aps := CollectScanResults(v, c.cfg.SSID, c.cfg.BSSID)
			s.aps = append(s.aps, aps...)

			if len(s.aps) == 0 && s.channelScanOffset == len(s.supportedChannels) {
				fmt.Printf("wlan: no matching network found for \"%s\" after a round of scanning\n", c.cfg.SSID)
			}

			// TODO(NET-420) find a way to stream results upward and let higher level
			// take care of de-duping
			if !s.running {
				if len(s.aps) > 0 {
					var sorted_aps = sortAndDedupeAPs(s.aps)
					c.ap = &sorted_aps[0]
					return newJoinState(), nil
				}
				s.channelScanOffset = 0
				s.aps = s.aps[:0]
				s.pause = true
			}
		}

		return s, nil

	default:
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *scanState) handleMLMETimeout(c *Client) (state, error) {
	if debug {
		log.Printf("scan timeout")
	}
	s.pause = false
	return s, nil
}

func (s *scanState) needTimer(c *Client) (bool, time.Duration) {
	if s.running {
		return false, 0
	}
	if c.cfg == nil || c.cfg.SSID == "" {
		return false, 0
	}
	scanInterval := DefaultScanInterval
	if c.cfg != nil && c.cfg.ScanInterval > 0 {
		scanInterval = time.Duration(c.cfg.ScanInterval) * time.Second
	}
	if debug {
		log.Printf("scan pause %v start", scanInterval)
	}
	return true, scanInterval
}

func (s *scanState) timerExpired(c *Client) (state, error) {
	if debug {
		log.Printf("scan pause finished")
	}
	s.pause = false
	return s, nil
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

func (s *joinState) run(c *Client) (time.Duration, error) {
	// Fail with error if network is an unsupported RSN.
	if c.ap.BSSDesc.Rsn != nil {
		if supported, err := eapol.IsRSNSupported(*c.ap.BSSDesc.Rsn); !supported {
			return InfiniteTimeout, err
		}
	}

	req := &mlme.JoinRequest{
		SelectedBss:        *c.ap.BSSDesc,
		JoinFailureTimeout: 20,
	}
	if debug {
		log.Printf("join req: %v", req)
	}

	return InfiniteTimeout, c.SendMessage(req, mlme.MlmeJoinReqOrdinal)
}

func (s *joinState) commandIsDisabled() bool {
	return true
}

func (s *joinState) handleCommand(cmd *commandRequest, c *Client) (state, error) {
	return s, nil
}

func (s *joinState) handleMLMEMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.JoinConfirm:
		if debug {
			PrintJoinConfirm(v)
		}

		if v.ResultCode == mlme.JoinResultCodesSuccess {
			return newAuthState(), nil
		} else {
			return newScanState(c), nil
		}
	default:
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *joinState) handleMLMETimeout(c *Client) (state, error) {
	return s, nil
}

func (s *joinState) needTimer(c *Client) (bool, time.Duration) {
	return false, 0
}

func (s *joinState) timerExpired(c *Client) (state, error) {
	return s, nil
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

func (s *authState) run(c *Client) (time.Duration, error) {
	req := &mlme.AuthenticateRequest{
		PeerStaAddress:     c.ap.BSSDesc.Bssid,
		AuthType:           mlme.AuthenticationTypesOpenSystem,
		AuthFailureTimeout: 20,
	}
	if debug {
		log.Printf("auth req: %v", req)
	}

	return InfiniteTimeout, c.SendMessage(req, mlme.MlmeAuthenticateReqOrdinal)
}

func (s *authState) commandIsDisabled() bool {
	return true
}

func (s *authState) handleCommand(cmd *commandRequest, c *Client) (state, error) {
	return s, nil
}

func (s *authState) handleMLMEMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.AuthenticateConfirm:
		if debug {
			PrintAuthenticateConfirm(v)
		}

		switch v.ResultCode {
		case mlme.AuthenticateResultCodesSuccess:
			return newAssocState(), nil
		case mlme.AuthenticateResultCodesAuthFailureTimeout:
			return newScanState(c), nil
		case mlme.AuthenticateResultCodesAuthenticationRejected:
			log.Printf("Authentication rejected by %v (%v), reset wlanstack state", c.cfg.SSID, c.cfg.BSSID)
			c.cfg = nil
			return newScanState(c), nil
		default:
			log.Printf("Authentication failed with result code %v, reset wlanstack state", v.ResultCode)
			c.cfg = nil
			return newScanState(c), nil
		}
	default:
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *authState) handleMLMETimeout(c *Client) (state, error) {
	return s, nil
}

func (s *authState) needTimer(c *Client) (bool, time.Duration) {
	return false, 0
}

func (s *authState) timerExpired(c *Client) (state, error) {
	return s, nil
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

func (s *assocState) run(c *Client) (time.Duration, error) {
	req := &mlme.AssociateRequest{
		PeerStaAddress: c.ap.BSSDesc.Bssid,
	}

	// If the network is an RSN, announce own cipher and authentication capabilities and configure
	// EAPOL client to process incoming EAPOL frames.
	bcnRawRSNE := c.ap.BSSDesc.Rsn
	if bcnRawRSNE != nil {
		bcnRSNE, err := elements.ParseRSN(*bcnRawRSNE)
		if err != nil {
			return InfiniteTimeout, fmt.Errorf("Error parsing Beacon RSNE")
		}

		assocRSNE := s.createAssociationRSNE(bcnRSNE)
		assocRawRSNE := assocRSNE.Bytes()
		req.Rsn = &assocRawRSNE

		supplicant := s.createSupplicant(c, bcnRSNE, assocRSNE)
		c.eapolC = s.createEAPOLClient(c, assocRSNE, supplicant)
	} else {
		c.eapolC = nil
	}

	if debug {
		log.Printf("assoc req: %v", req)
	}

	return InfiniteTimeout, c.SendMessage(req, mlme.MlmeAssociateReqOrdinal)
}

// Creates the RSNE used in MLME-Association.request to announce supported ciphers and AKMs to the
// Supported Ciphers and AKMs:
// AKMS: PSK
// Pairwise: CCMP-128
// Group: CCMP-128, TKIP
func (s *assocState) createAssociationRSNE(bcnRSNE *elements.RSN) *elements.RSN {
	rsne := elements.NewEmptyRSN()
	rsne.GroupData = &elements.CipherSuite{
		Type: elements.CipherSuiteType_CCMP128,
		OUI:  elements.DefaultCipherSuiteOUI,
	}
	// If GroupCipher does not support CCMP-128, fallback to TKIP.
	// Note: IEEE allows the usage of Group Ciphers which are less secure than Pairwise ones. TKIP
	// is supported for Group Ciphers solely for compatibility reasons. TKIP is considered broken
	// and will not be supported for pairwise cipher usage, not even to support compatibility with
	// older devices.
	if !bcnRSNE.GroupData.IsIn(elements.CipherSuiteType_CCMP128) {
		rsne.GroupData.Type = elements.CipherSuiteType_TKIP
	}
	rsne.PairwiseCiphers = []elements.CipherSuite{
		{
			Type: elements.CipherSuiteType_CCMP128,
			OUI:  elements.DefaultCipherSuiteOUI,
		},
	}
	rsne.AKMs = []elements.AKMSuite{
		{
			Type: elements.AkmSuiteType_PSK,
			OUI:  elements.DefaultCipherSuiteOUI,
		},
	}
	capabilities := uint16(0)
	rsne.Caps = &capabilities
	return rsne
}

func (s *assocState) createSupplicant(c *Client, bcnRSNE *elements.RSN, assocRSNE *elements.RSN) eapol.KeyExchange {
	password := ""
	if c.cfg != nil {
		password = c.cfg.Password
	}
	// TODO(hahnr): Add support for authentication selection once we support other AKMs.
	config := handshake.FourWayConfig{
		Transport:  &eapol.SMETransport{SME: c},
		PassPhrase: password,
		SSID:       c.ap.SSID,
		PeerAddr:   c.ap.BSSID,
		StaAddr:    c.staAddr,
		AssocRSNE:  assocRSNE,
		BeaconRSNE: bcnRSNE,
	}
	hs := handshake.NewFourWay(config)
	return handshake.NewSupplicant(hs)
}

func (s *assocState) createEAPOLClient(c *Client, assocRSNE *elements.RSN, keyExchange eapol.KeyExchange) *eapol.Client {
	// TODO(hahnr): Derive MIC size from AKM.
	return eapol.NewClient(eapol.Config{128, keyExchange})
}

func (s *assocState) commandIsDisabled() bool {
	return true
}

func (s *assocState) handleCommand(cmd *commandRequest, c *Client) (state, error) {
	return s, nil
}

func (s *assocState) handleMLMEMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.AssociateConfirm:
		if debug {
			PrintAssociateConfirm(v)
		}

		if v.ResultCode == mlme.AssociateResultCodesSuccess {
			if c.eapolC == nil {
				log.Printf("WLAN connected (Open Authentication)")
			}
			return newAssociatedState(), nil
		} else {
			return newScanState(c), nil
		}
	default:
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *assocState) handleMLMETimeout(c *Client) (state, error) {
	return s, nil
}

func (s *assocState) needTimer(c *Client) (bool, time.Duration) {
	return false, 0
}

func (s *assocState) timerExpired(c *Client) (state, error) {
	return s, nil
}

// Associated

type associatedState struct {
	scanner *scanState
}

func newAssociatedState() *associatedState {
	return &associatedState{scanner: nil}
}

func (s *associatedState) String() string {
	return "associated"
}

func (s *associatedState) run(c *Client) (time.Duration, error) {
	if s.scanner != nil {
		return s.scanner.run(c)
	}
	return InfiniteTimeout, nil
}

func (s *associatedState) commandIsDisabled() bool {
	// TODO(toshik): disable if Scan request is running
	return false
}

func (s *associatedState) handleCommand(cmd *commandRequest, c *Client) (state, error) {
	switch cmd.id {
	case CmdDisconnect:
		req := &mlme.DeauthenticateRequest{
			PeerStaAddress: c.ap.BSSID,
			// TODO(hahnr): Map Reason Codes to strings and provide map.
			ReasonCode: 36, // Requesting STA is leaving the BSS
		}
		if debug {
			log.Printf("deauthenticate req: %v", req)
		}

		err := c.SendMessage(req, mlme.MlmeDeauthenticateReqOrdinal)
		res := &CommandResult{}
		if err != nil {
			res.Err = &wlan_service.Error{wlan_service.ErrCodeInternal, "Could not send MLME request"}
		}
		cmd.respC <- res

	// TODO(NET-488, NET-491): Lift up this workaround, and support scanning in Associated state.
	// case CmdScan:
	//	if s.scanner == nil {
	//		s.scanner = newScanState(c)
	//	}
	//	s.scanner.handleCommand(cmd, c)
	default:
		cmd.respC <- &CommandResult{nil,
			&wlan_service.Error{wlan_service.ErrCodeNotSupported,
				"Can't run the command in associatedState"}}
	}
	return s, nil
}

func (s *associatedState) handleMLMEMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.DisassociateIndication:
		if debug {
			PrintDisassociateIndication(v)
		}
		return newAssocState(), nil
	case *mlme.DeauthenticateConfirm:
		if debug {
			PrintDeauthenticateConfirm(v)
		}
		// This was a user issued deauthentication. Clear config to prevent automatic reconnect, and
		// enter scan state.
		c.cfg = nil
		return newScanState(c), nil
	case *mlme.DeauthenticateIndication:
		if debug {
			PrintDeauthenticateIndication(v)
		}
		if v.ReasonCode == mlme.ReasonCodeInvalidAuthentication { // INVALID_AUTHENTICATION
			log.Printf("Invalid authentication (possibly a wrong password?) with %v (%v), reset wlanstack state", c.cfg.SSID, c.cfg.BSSID)
		} else {
			log.Printf("DeauthenticateIndication received, reason code: %v, reset wlanstack state", v.ReasonCode)
		}
		c.cfg = nil
		return newScanState(c), nil
	case *mlme.SignalReportIndication:
		if debug {
			PrintSignalReportIndication(v)
		}
		c.ap.RssiDbm = v.RssiDbm
		return s, nil
	case *mlme.EapolIndication:
		if c.eapolC != nil {
			c.eapolC.HandleEAPOLFrame(v.Data)
		}
		return s, nil
	case *mlme.EapolConfirm:
		// TODO(hahnr): Evaluate response code.
		if c.eapolC.KeyExchange().IsComplete() {
			log.Printf("WLAN connected (EAPOL)")
			c.cfg.SaveConfigUser()
		}
		return s, nil
	default:
		if s.scanner != nil {
			_, err := s.scanner.handleMLMEMsg(msg, c)
			if s.scanner.completed {
				s.scanner = nil
			}
			return s, err
		}
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *associatedState) handleMLMETimeout(c *Client) (state, error) {
	if s.scanner != nil {
		_, err := s.scanner.handleMLMETimeout(c)
		return s, err
	}
	return s, nil
}

func (s *associatedState) needTimer(c *Client) (bool, time.Duration) {
	if s.scanner != nil {
		return s.scanner.needTimer(c)
	}
	return false, 0
}

func (s *associatedState) timerExpired(c *Client) (state, error) {
	if s.scanner != nil {
		_, err := s.scanner.timerExpired(c)
		return s, err
	}
	return s, nil
}
