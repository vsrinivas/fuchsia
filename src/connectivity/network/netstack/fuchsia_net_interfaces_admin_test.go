// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netstack

import (
	"context"
	"fmt"
	"syscall/zx"
	"syscall/zx/fidl"
	"syscall/zx/zxwait"
	"testing"
	"time"
	"unsafe"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/netdevice"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
	"go.fuchsia.dev/fuchsia/src/lib/component"

	"fidl/fuchsia/hardware/network"
	"fidl/fuchsia/net/interfaces/admin"
)

var _ network.DeviceWithCtx = (*fakeNetworkDeviceImpl)(nil)

type fakeNetworkDeviceImpl struct {
	t                 *testing.T
	rx, tx            zx.Handle
	data, descriptors zx.VMO
	sessionReq        network.SessionWithCtxInterfaceRequest
}

const fakeDepth = 128

func (f *fakeNetworkDeviceImpl) GetInfo(fidl.Context) (network.DeviceInfo, error) {
	var info network.DeviceInfo
	info.SetMinDescriptorLength(uint8(netdevice.DescriptorLength / 8))
	info.SetDescriptorVersion(netdevice.DescriptorVersion)
	info.SetRxDepth(fakeDepth)
	info.SetTxDepth(fakeDepth)
	info.SetBufferAlignment(1)
	info.SetMaxBufferLength(2048)
	info.SetMinRxBufferLength(2048)
	info.SetMinTxBufferLength(0)
	info.SetMinTxBufferHead(0)
	info.SetMinTxBufferTail(0)
	info.SetMaxBufferParts(1)
	return info, nil
}

func (f *fakeNetworkDeviceImpl) OpenSession(_ fidl.Context, sessionName string, sessionInfo network.SessionInfo) (network.DeviceOpenSessionResult, error) {
	const elemCount = fakeDepth * 2
	const elemSize = 2

	f.data = sessionInfo.Data
	f.descriptors = sessionInfo.Descriptors

	t := f.t
	var rx, tx zx.Handle
	if status := zx.Sys_fifo_create(elemCount, elemSize, 0, &rx, &f.rx); status != zx.ErrOk {
		err := &zx.Error{Status: status, Text: "FIFO create"}
		t.Errorf("zx.Sys_fifo_create(_, _, _, _) = %s", err)
		return network.DeviceOpenSessionResult{}, err
	}
	t.Cleanup(func() {
		if err := rx.Close(); err != nil {
			t.Errorf("rx.Close() = %s", err)
		}
	})

	if status := zx.Sys_fifo_create(elemCount, elemSize, 0, &tx, &f.tx); status != zx.ErrOk {
		err := &zx.Error{Status: status, Text: "FIFO create"}
		t.Errorf("zx.Sys_fifo_create(_, _, _, _) = %s", err)
		return network.DeviceOpenSessionResult{}, err
	}
	t.Cleanup(func() {
		if err := tx.Close(); err != nil {
			t.Errorf("tx.Close() = %s", err)
		}
	})

	sessionReq, session, err := network.NewSessionWithCtxInterfaceRequest()
	if err != nil {
		t.Errorf("network.NewSessionWithCtxInterfaceRequest() = %s", err)
		return network.DeviceOpenSessionResult{}, err
	}

	f.sessionReq = sessionReq

	result := network.DeviceOpenSessionResultWithResponse(
		network.DeviceOpenSessionResponse{
			Session: *session,
			Fifos:   network.Fifos{Rx: rx, Tx: tx},
		})

	// Prevent test cleanup.
	rx = zx.HandleInvalid
	tx = zx.HandleInvalid

	return result, nil
}

func (f *fakeNetworkDeviceImpl) GetPort(_ fidl.Context, id network.PortId, _ network.PortWithCtxInterfaceRequest) error {
	f.t.Fatalf("unexpected GetPort(_,  %#v, _) call", id)
	return nil
}

func (f *fakeNetworkDeviceImpl) GetPortWatcher(fidl.Context, network.PortWatcherWithCtxInterfaceRequest) error {
	f.t.Fatalf("unexpected GetPortWatcher(_, _) call")
	return nil
}

func (f *fakeNetworkDeviceImpl) Clone(fidl.Context, network.DeviceWithCtxInterfaceRequest) error {
	f.t.Fatalf("unexpected Clone(_, _) call")
	return nil
}

func TestClosesNetworkDevice(t *testing.T) {
	ns, _ := newNetstack(t, netstackTestOptions{})
	installer := &interfacesAdminInstallerImpl{ns: ns}

	const (
		dropDevice = iota
		dropControl
	)

	for _, drop := range []int{dropDevice, dropControl} {
		for _, detach := range []bool{true, false} {
			if drop == dropControl && detach {
				// Dropping control with detach is not a valid use case since it doesn't
				// cause the device to close.
				continue
			}
			name := fmt.Sprintf("drop%s%s", func() string {
				switch drop {
				case dropDevice:
					return "Device"
				case dropControl:
					return "Control"
				default:
					panic(fmt.Sprintf("unknown drop value %d", drop))
				}
			}(), func() string {
				if detach {
					return "Detach"
				}
				return "NoDetach"
			}())

			t.Run(name, func(t *testing.T) {
				deviceReq, device, err := network.NewDeviceWithCtxInterfaceRequest()
				if err != nil {
					t.Fatalf("network.NewDeviceWithCtxInterfaceRequest() = %s", err)
				}
				t.Cleanup(func() {
					if err := deviceReq.Close(); err != nil {
						t.Errorf("deviceReq.Close() = %s", err)
					}
					if err := device.Close(); err != nil {
						t.Errorf("device.Close() = %s", err)
					}
				})

				controlReq, control, err := admin.NewDeviceControlWithCtxInterfaceRequest()
				if err != nil {
					t.Fatalf("admin.NewDeviceControlWithCtxInterfaceRequest() = %s", err)
				}
				t.Cleanup(func() {
					if err := controlReq.Close(); err != nil {
						t.Errorf("controlReq.Close() = %s", err)
					}
					if err := control.Close(); err != nil {
						t.Errorf("control.Close() = %s", err)
					}
				})

				if detach {
					if err := control.Detach(context.Background()); err != nil {
						t.Fatalf("control.Detach(_) = %s", err)
					}
				}

				fakeDevice := &fakeNetworkDeviceImpl{t: t}
				t.Cleanup(func() {
					if err := fakeDevice.tx.Close(); err != nil {
						t.Errorf("fakeDevice.tx.Close() = %s", err)
					}
					if err := fakeDevice.rx.Close(); err != nil {
						t.Errorf("fakeDevice.rx.Close() = %s", err)
					}
					if err := fakeDevice.sessionReq.Close(); err != nil {
						t.Errorf("fakeDevice.sessionReq.Close() = %s", err)
					}
					if err := fakeDevice.data.Close(); err != nil {
						t.Errorf("fakeDevice.data.Close() = %s", err)
					}
					if err := fakeDevice.descriptors.Close(); err != nil {
						t.Errorf("fakeDevice.descriptors.Close() = %s", err)
					}
				})

				ctx, cancel := context.WithCancel(context.Background())
				var wg sync.WaitGroup
				wg.Add(1)

				defer func() {
					cancel()
					wg.Wait()
				}()

				go func() {
					defer wg.Done()
					component.Serve(ctx, &network.DeviceWithCtxStub{Impl: fakeDevice}, deviceReq.Channel, component.ServeOptions{
						OnError: func(err error) {
							t.Errorf("OnError: %s", err)
						},
						// Channel is closed by test cleanup.
						KeepChannelAlive: true,
					})
				}()

				if err := installer.InstallDevice(context.Background(), *device, controlReq); err != nil {
					t.Fatalf("installer.InstallDevice(_, _, _) = %s", err)
				}
				// Prevent test cleanup.
				*device.Handle() = zx.HandleInvalid
				*controlReq.Handle() = zx.HandleInvalid

				channelHandle := func() *zx.Handle {
					switch drop {
					case dropDevice:
						// Drop the device and observe control handle close.
						if err := fakeDevice.rx.Close(); err != nil {
							t.Fatalf("fakeDevice.rx.Close() = %s", err)
						}
						return control.Handle()
					case dropControl:
						// Drop control and observe the session handle close.
						if err := control.Close(); err != nil {
							t.Fatalf("control.Close() = %s", err)
						}
						return fakeDevice.sessionReq.Handle()
					default:
						panic(fmt.Sprintf("unknown drop value %d", drop))
					}

				}()

				signals, err := zxwait.WaitContext(context.Background(), *channelHandle, zx.SignalChannelPeerClosed)
				if err != nil {
					t.Fatalf("zxwait.Waitcontext(_, _, _) = %s", err)
				}
				if got, want := signals, zx.Signals(zx.SignalChannelPeerClosed); got != want {
					t.Errorf("got zxwait.WaitContext(_, _, _) = %d, want %d", got, want)
				}

				// There's no synchronization guaranteeing that the VMOs are already unmapped.
				// We must poll until we observe zero mappings.
				waitVmoUnmapped := func(name string, vmo zx.VMO) {
					for {
						var info zx.InfoVmo
						if err := vmo.Handle().GetInfo(zx.ObjectInfoVMO, unsafe.Pointer(&info), uint(unsafe.Sizeof(info))); err != nil {
							t.Fatalf("%s vmo.Handle().GetInfo(_, _, _) = %s", name, err)
						}
						switch n := info.NumMappings; n {
						case 0:
							return
						case 1:
							t.Logf("%s VMO still has one mapping, waiting...", name)
							time.Sleep(10 * time.Millisecond)
						default:
							t.Fatalf("%s info.NumMappings = %d, expected <= 1", name, n)
						}
					}
				}
				waitVmoUnmapped("data", fakeDevice.data)
				waitVmoUnmapped("descriptors", fakeDevice.descriptors)
			})

		}
	}
}
