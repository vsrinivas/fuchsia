// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package deviceid generates device IDs.
package deviceid

// DeviceID generates the device ID from a MAC address.
//
// This is used for mDNS, and the device ID for the first
// ethernet device in the system is the default host name
// of a Fuchsia device.
func DeviceID(mac [6]byte) string {
	return dict(uint16(mac[0])|((uint16(mac[4])<<8)&0xF00)) + "-" +
		dict(uint16(mac[1])|((uint16(mac[5])<<8)&0xF00)) + "-" +
		dict(uint16(mac[2])|((uint16(mac[4])<<4)&0xF00)) + "-" +
		dict(uint16(mac[3])|((uint16(mac[5])<<4)&0xF00))
}

func dict(x uint16) string {
	return dictionary[int(x)%len(dictionary)]
}
