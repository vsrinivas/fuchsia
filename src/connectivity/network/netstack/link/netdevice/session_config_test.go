// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netdevice

import (
	"errors"
	"testing"

	"fidl/fuchsia/hardware/network"

	"github.com/google/go-cmp/cmp"
)

func TestMakeSessionConfig(t *testing.T) {
	const txBufferHead = 16
	const txBufferTail = 8
	var baseInfo network.DeviceInfo
	baseInfo.SetMaxBufferLength(16 * 1024)
	baseInfo.SetMinRxBufferLength(0)
	baseInfo.SetRxDepth(16)
	baseInfo.SetTxDepth(16)
	baseInfo.SetBufferAlignment(1)
	baseInfo.SetMinTxBufferHead(txBufferHead)
	baseInfo.SetMinTxBufferTail(txBufferTail)

	factory := SimpleSessionConfigFactory{}

	tests := []struct {
		name           string
		updateInfo     func(*network.DeviceInfo)
		expectedConfig SessionConfig
	}{
		{
			name: "defaults",
			expectedConfig: SessionConfig{
				BufferLength:      DefaultBufferLength,
				BufferStride:      DefaultBufferLength,
				DescriptorLength:  descriptorLength,
				TxHeaderLength:    baseInfo.MinTxBufferHead,
				TxTailLength:      baseInfo.MinTxBufferTail,
				RxDescriptorCount: baseInfo.TxDepth,
				TxDescriptorCount: baseInfo.RxDepth,
				Options:           network.SessionFlagsPrimary,
			},
		},
		{
			name: "respect max buffer length",
			updateInfo: func(info *network.DeviceInfo) {
				info.SetMaxBufferLength(DefaultBufferLength / 4)
			},
			expectedConfig: SessionConfig{
				BufferLength:      DefaultBufferLength / 4,
				BufferStride:      DefaultBufferLength / 4,
				DescriptorLength:  descriptorLength,
				TxHeaderLength:    baseInfo.MinTxBufferHead,
				TxTailLength:      baseInfo.MinTxBufferTail,
				RxDescriptorCount: baseInfo.TxDepth,
				TxDescriptorCount: baseInfo.RxDepth,
				Options:           network.SessionFlagsPrimary,
			},
		},
		{
			name: "respect min buffer length",
			updateInfo: func(info *network.DeviceInfo) {
				info.SetMinRxBufferLength(DefaultBufferLength * 2)
			},
			expectedConfig: SessionConfig{
				BufferLength:      DefaultBufferLength * 2,
				BufferStride:      DefaultBufferLength * 2,
				DescriptorLength:  descriptorLength,
				TxHeaderLength:    baseInfo.MinTxBufferHead,
				TxTailLength:      baseInfo.MinTxBufferTail,
				RxDescriptorCount: baseInfo.TxDepth,
				TxDescriptorCount: baseInfo.RxDepth,
				Options:           network.SessionFlagsPrimary,
			},
		},
		{
			name: "buffer alignment",
			updateInfo: func(info *network.DeviceInfo) {
				info.SetBufferAlignment(64)
				info.SetMinRxBufferLength(DefaultBufferLength + 112)
			},
			expectedConfig: SessionConfig{
				BufferLength:      DefaultBufferLength + 112,
				BufferStride:      DefaultBufferLength + 128,
				DescriptorLength:  descriptorLength,
				TxHeaderLength:    baseInfo.MinTxBufferHead,
				TxTailLength:      baseInfo.MinTxBufferTail,
				RxDescriptorCount: baseInfo.TxDepth,
				TxDescriptorCount: baseInfo.RxDepth,
				Options:           network.SessionFlagsPrimary,
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			info := baseInfo
			if test.updateInfo != nil {
				test.updateInfo(&info)
			}
			sessionConfig, err := factory.MakeSessionConfig(info)
			if err != nil {
				t.Fatalf("MakeSessionConfig(%+v): %s", info, err)
			}
			if diff := cmp.Diff(test.expectedConfig, sessionConfig); diff != "" {
				t.Errorf("MakeSessionConfig(%+v): (-want +got)\n%s", info, diff)
			}
		})
	}
}

func TestCheckSessionConfig(t *testing.T) {
	config := SessionConfig{
		BufferLength:   DefaultBufferLength,
		TxHeaderLength: 16,
		TxTailLength:   8,
	}

	tests := []struct {
		name  string
		mtu   uint32
		check func(t *testing.T, portStatus network.PortStatus, err error)
	}{
		{
			name: "success",
			mtu:  config.BufferLength - uint32(config.TxHeaderLength+config.TxTailLength),
			check: func(t *testing.T, portStatus network.PortStatus, err error) {
				if err != nil {
					t.Fatalf("config.checkValidityForPort(%+v) = %s", portStatus, err)
				}
			},
		},
		{
			name: "failure",
			mtu:  config.BufferLength,
			check: func(t *testing.T, portStatus network.PortStatus, err error) {
				var got *insufficientBufferLengthError
				if !errors.As(err, &got) {
					t.Fatalf("checkValidityForPort(%+v) = %s, expected %T", portStatus, err, got)
				}
				if diff := cmp.Diff(got, &insufficientBufferLengthError{
					bufferLength: config.BufferLength,
					bufferHeader: config.TxHeaderLength,
					bufferTail:   config.TxTailLength,
					mtu:          config.BufferLength,
				}, cmp.AllowUnexported(*got)); diff != "" {
					t.Errorf("checkValidityForPort(%+v) error diff: (-want +got)\n%s", portStatus, diff)
				}
			},
		},
	}
	for _, testCase := range tests {
		t.Run(testCase.name, func(t *testing.T) {
			var portStatus network.PortStatus
			portStatus.SetMtu(testCase.mtu)
			err := config.checkValidityForPort(portStatus)
			testCase.check(t, portStatus, err)
		})
	}
}
