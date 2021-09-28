// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netdevice

import (
	"fmt"

	"fidl/fuchsia/hardware/network"
)

// SessionConfigFactory creates session configurations from device information.
type SessionConfigFactory interface {
	MakeSessionConfig(deviceInfo network.DeviceInfo) (SessionConfig, error)
}

// SessionConfig holds configuration used to open a session with a network
// device.
type SessionConfig struct {
	// Length of each buffer.
	BufferLength uint32
	// Buffer stride on VMO.
	BufferStride uint32
	// Descriptor length, in bytes.
	DescriptorLength uint64
	// Tx header length, in bytes.
	TxHeaderLength uint16
	// Tx tail length, in bytes.
	TxTailLength uint16
	// Number of rx descriptors to allocate.
	RxDescriptorCount uint16
	// Number of tx descriptors to allocate.
	TxDescriptorCount uint16
	// Session flags.
	Options network.SessionFlags
}

// DefaultBufferLength is the buffer length used by SimpleSessionConfigFactory.
const DefaultBufferLength uint32 = 2048

// SimpleSessionConfigFactory is the default configuration factory.
type SimpleSessionConfigFactory struct{}

// MakeSessionConfig implements SessionConfigFactory.
func (c *SimpleSessionConfigFactory) MakeSessionConfig(deviceInfo network.DeviceInfo) (SessionConfig, error) {
	bufferLength := DefaultBufferLength
	if bufferLength > deviceInfo.MaxBufferLength {
		bufferLength = deviceInfo.MaxBufferLength
	}
	if bufferLength < deviceInfo.MinRxBufferLength {
		bufferLength = deviceInfo.MinRxBufferLength
	}

	config := SessionConfig{
		BufferLength:      bufferLength,
		BufferStride:      bufferLength,
		DescriptorLength:  descriptorLength,
		TxHeaderLength:    deviceInfo.MinTxBufferHead,
		TxTailLength:      deviceInfo.MinTxBufferTail,
		RxDescriptorCount: deviceInfo.RxDepth,
		TxDescriptorCount: deviceInfo.TxDepth,
		Options:           network.SessionFlagsPrimary,
	}
	align := deviceInfo.BufferAlignment
	if config.BufferStride%align != 0 {
		// Align back.
		config.BufferStride -= config.BufferStride % align
		// Align up if we have space.
		if config.BufferStride+align <= deviceInfo.MaxBufferLength {
			config.BufferStride += align
		}
	}
	return config, nil
}

type insufficientBufferLengthError struct {
	bufferLength uint32
	bufferHeader uint16
	bufferTail   uint16
	mtu          uint32
}

func (e *insufficientBufferLengthError) Error() string {
	return fmt.Sprintf("buffer=%d < header=%d + tail=%d + mtu=%d", e.bufferLength, e.bufferHeader, e.bufferTail, e.mtu)
}

func (c *SessionConfig) checkValidityForPort(portStatus network.PortStatus) error {
	mtu := portStatus.GetMtu()
	if c.BufferLength < uint32(c.TxHeaderLength)+uint32(c.TxTailLength)+mtu {
		return &insufficientBufferLengthError{
			bufferLength: c.BufferLength,
			bufferHeader: c.TxHeaderLength,
			bufferTail:   c.TxTailLength,
			mtu:          mtu,
		}
	}
	return nil
}
