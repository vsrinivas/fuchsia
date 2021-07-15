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

// A factory of session configurations from device information.
// A default implementation is provided by SimpleSessionConfigFactory.
type SessionConfigFactory interface {
	// Creates a SessionConfig for a given network device based on the provided
	// deviceInfo and portStatus.
	MakeSessionConfig(deviceInfo network.DeviceInfo, portStatus network.PortStatus) (SessionConfig, error)
}

// Configuration used to open a session with a network device.
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
	// Types of rx frames to subscribe to.
	RxFrames []network.FrameType
}

// The buffer length used by SimpleSessionConfigFactory.
const DefaultBufferLength uint32 = 2048

// A simple session configuration factory.
type SimpleSessionConfigFactory struct {
	// The frame types to subscribe to. Will subscribe to all frame types if
	// empty.
	FrameTypes []network.FrameType
}

type InsufficientBufferLengthError struct {
	BufferLength uint32
	BufferHeader uint16
	BufferTail   uint16
	MTU          uint32
}

func (e *InsufficientBufferLengthError) Error() string {
	return fmt.Sprintf("buffer length=%d < header=%d + tail=%d + mtu=%d", e.BufferLength, e.BufferHeader, e.BufferTail, e.MTU)
}

// MakeSessionConfig implements SessionConfigFactory.
func (c *SimpleSessionConfigFactory) MakeSessionConfig(deviceInfo network.DeviceInfo, portStatus network.PortStatus) (SessionConfig, error) {
	bufferLength := DefaultBufferLength
	if bufferLength > deviceInfo.MaxBufferLength {
		bufferLength = deviceInfo.MaxBufferLength
	}
	if bufferLength < deviceInfo.MinRxBufferLength {
		bufferLength = deviceInfo.MinRxBufferLength
	}

	if bufferLength < uint32(deviceInfo.MinTxBufferHead)+uint32(deviceInfo.MinTxBufferTail)+portStatus.GetMtu() {
		return SessionConfig{}, &InsufficientBufferLengthError{
			BufferLength: bufferLength,
			BufferHeader: deviceInfo.MinTxBufferHead,
			BufferTail:   deviceInfo.MinTxBufferTail,
			MTU:          portStatus.GetMtu(),
		}
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
		RxFrames:          c.FrameTypes,
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
