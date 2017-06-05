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
	"syscall"
	"syscall/mx"
	"syscall/mx/mxerror"
	"time"
)

const MX_SOCKET_READABLE = mx.SignalObject0
const SLEEP_INTERVAL = 5 * time.Second

type Client struct {
	f        *os.File
	wlanChan mx.Channel
}

func NewClient(path string) (*Client, error) {
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
		f:        f,
		wlanChan: mx.Channel{ch},
	}
	return c, nil
}

func (c *Client) Scan() {
	channels := []uint16{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}
	req := &mlme.ScanRequest{
		BssType:        mlme.BssTypes_Infrastructure,
		ScanType:       mlme.ScanTypes_Passive,
		ChannelList:    &channels,
		MinChannelTime: 100,
		MaxChannelTime: 300,
	}
	log.Printf("req: %v", req)

	enc := bindings.NewEncoder()
	// Create a method call header, similar to that of FIDL2
	enc.StartStruct(16, 0)
	if err := enc.WriteUint64(1); err != nil {
		log.Printf("could not encode txid: %v", err)
		return
	}
	if err := enc.WriteUint32(0); err != nil {
		log.Printf("could not encode flags: %v", err)
		return
	}
	if err := enc.WriteInt32(int32(mlme.Method_ScanRequest)); err != nil {
		log.Printf("could not encode ordinal: %v", err)
		return
	}
	enc.Finish()
	if err := req.Encode(enc); err != nil {
		log.Printf("could not encode ScanRequest: %v", err)
		return
	}

	reqBuf, _, encErr := enc.Data()
	if encErr != nil {
		log.Printf("could not get encoding data: %v", encErr)
		return
	}
	log.Printf("encoded ScanRequest: %v", reqBuf)
	if err := c.wlanChan.Write(reqBuf, nil, 0); err != nil {
		log.Printf("could not write to wlan channel: %v", err)
		return
	}

	for {
		obs, err := c.wlanChan.Handle.WaitOne(mx.SignalChannelReadable|mx.SignalChannelPeerClosed, mx.TimensecInfinite)
		switch mxerror.Status(err) {
		case mx.ErrBadHandle, mx.ErrCanceled, mx.ErrPeerClosed:
			log.Printf("error waiting on handle: %v", err)
			return
		case mx.ErrOk:
			switch {
			case obs&mx.SignalChannelPeerClosed != 0:
				log.Printf("channel closed")
				return
			case obs&MX_SOCKET_READABLE != 0:
				var buf [4096]byte
				_, _, err := c.wlanChan.Read(buf[:], nil, 0)
				if err != nil {
					log.Printf("error reading from channel: %v", err)
					break
				}

				dec := bindings.NewDecoder(buf[:], nil)
				var header APIHeader
				if err := header.Decode(dec); err != nil {
					log.Printf("could not decode api header: %v", err)
					break
				}
				switch header.ordinal {
				case int32(mlme.Method_ScanConfirm):
					var resp mlme.ScanResponse
					if err := resp.Decode(dec); err != nil {
						log.Printf("could not decode ScanResponse: %v", err)
					} else {
						PrintScanResponse(&resp)
					}
					go func() {
						time.Sleep(SLEEP_INTERVAL)
						if err := c.wlanChan.Write(reqBuf, nil, 0); err != nil && mxerror.Status(err) != mx.ErrPeerClosed {
							log.Printf("could not write to wlan channel: %v", err)
						}
					}()
				default:
					log.Printf("unknown message ordinal: %v", header.ordinal)
				}
			}
		}
	}
}
