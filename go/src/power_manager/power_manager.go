// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"app/context"
	"bytes"
	"encoding/binary"
	"fidl/bindings"
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"sync"
	"syscall"
	"syscall/mx"
	"syscall/mx/mxerror"
	"syscall/mx/fdio"
	"time"

	"garnet/public/lib/power/fidl/power_manager"
)

var (
	updateWaitTimeFlag uint
	logger             = log.New(os.Stdout, "power_manager: ", log.Lshortfile)
)

const (
	powerDevice = "/dev/class/power"
)

func init() {
	flag.UintVar(&updateWaitTimeFlag, "update-wait-time", 180, "Time to sleep between status update in seconds.")
}

type PowerManager struct {
	mu                     sync.Mutex
	batteryStatus          power_manager.BatteryStatus
	watchers               []*power_manager.PowerManagerWatcher_Proxy
	powerAdapterTimeStamp  int64
	batteryStatusTimeStamp int64
}

func (pm *PowerManager) GetBatteryStatus() (power_manager.BatteryStatus, error) {
	logger.Println("GetBatteryStatus")
	pm.mu.Lock()
	defer pm.mu.Unlock()

	return pm.batteryStatus, nil
}

func (pm *PowerManager) Watch(watcher power_manager.PowerManagerWatcher_Pointer) error {
	pmw := power_manager.NewProxyForPowerManagerWatcher(watcher, bindings.GetAsyncWaiter())
	pm.mu.Lock()
	pm.watchers = append(pm.watchers, pmw)
	pm.mu.Unlock()
	go pmw.OnChangeBatteryStatus(pm.batteryStatus)
	return nil
}

func getPowerInfo(m fdio.FDIO) (*fdio.PowerInfoResult, error) {
	var pi fdio.PowerInfoResult
	buf := new(bytes.Buffer)
	// LE for our arm64 and amd64 archs
	err := binary.Write(buf, binary.LittleEndian, pi)
	if err != nil {
		return nil, fmt.Errorf("binary.Write failed: %s", err)
	}
	b := buf.Bytes()

	_, err = m.Ioctl(fdio.IoctlPowerGetInfo, nil, b)
	if err != nil {
		return nil, fmt.Errorf("ioctl err: %s", err)
	}
	buf = bytes.NewBuffer(b)
	err = binary.Read(buf, binary.LittleEndian, &pi)
	if err != nil {
		return nil, fmt.Errorf("binary.Read failed: %s", err)
	}
	return &pi, nil
}

func getBatteryInfo(m fdio.FDIO) (*fdio.BatteryInfoResult, error) {
	var bi fdio.BatteryInfoResult
	buf := new(bytes.Buffer)
	// LE for our arm64 and amd64 archs
	err := binary.Write(buf, binary.LittleEndian, bi)
	if err != nil {
		return nil, fmt.Errorf("binary.Write failed: %s", err)
	}
	b := buf.Bytes()

	_, err = m.Ioctl(fdio.IoctlPowerGetBatteryInfo, nil, b)
	if err != nil {
		return nil, fmt.Errorf("ioctl err: %s", err)
	}
	buf = bytes.NewBuffer(b)
	err = binary.Read(buf, binary.LittleEndian, &bi)
	if err != nil {
		return nil, fmt.Errorf("binary.Read failed: %s", err)
	}
	return &bi, nil
}

func addListener(m fdio.FDIO, callback func(fdio.FDIO)) error {
	handles, err := m.Ioctl(fdio.IoctlPowerGetStateChangeEvent, nil, nil)
	if err != nil {
		return fmt.Errorf("ioctl err: %s", err)
	}
	if len(handles) != 1 {
		return fmt.Errorf("Ioctl did not return correct number of handles, expected 1 got %d", len(handles))
	}
	go func() {
		for {
			wi := []mx.WaitItem{
				mx.WaitItem{
					Handle:  handles[0],
					WaitFor: mx.SignalUser0,
					Pending: 0,
				},
			}
			if err := mx.WaitMany(wi, mx.TimensecInfinite); err != nil {
				logger.Printf("Error while waiting: %s\n", err)
				break
			} else {
				callback(m)
			}
		}
	}()
	return nil
}

// Updates the status
func (pm *PowerManager) updateStatus(m fdio.FDIO) error {
	pi, err := getPowerInfo(m)
	if err != nil {
		return fmt.Errorf("Failed to get power info: %s\n", err)
	}
	var bi *fdio.BatteryInfoResult
	if pi.PowerType == 1 {
		bi, err = getBatteryInfo(m)
		if err != nil {
			return fmt.Errorf("Failed to get battery info: %s\n", err)
		}
	}
	now := time.Now().UnixNano()
	pm.mu.Lock()
	defer pm.mu.Unlock()
	oldStatus := pm.batteryStatus
	t := pm.powerAdapterTimeStamp
	if pi.PowerType == 1 {
		t = pm.batteryStatusTimeStamp
	}
	if t > now {
		// stale no need to update, return
		return nil
	}

	if pi.PowerType == 1 {
		pm.batteryStatusTimeStamp = now
		pm.batteryStatus.BatteryPresent = pi.State&fdio.PowerStateOnline > 0
		pm.batteryStatus.Charging = pi.State&fdio.PowerStateCharging > 0
		pm.batteryStatus.Discharging = pi.State&fdio.PowerStateDischarging > 0
		pm.batteryStatus.Critical = pi.State&fdio.PowerStateCritical > 0
		if pm.batteryStatus.BatteryPresent {
			pm.batteryStatus.Level = float32(bi.RemainingCapacity) * 100 / float32(bi.LastFullCapacity)
			if bi.PresentRate < 0 {
				pm.batteryStatus.RemainingBatteryLife = float32(bi.RemainingCapacity) / float32(bi.PresentRate*-1)
			} else {
				pm.batteryStatus.RemainingBatteryLife = -1
			}
		}
	} else {
		pm.powerAdapterTimeStamp = now
		pm.batteryStatus.PowerAdapterOnline = pi.State&fdio.PowerStateOnline > 0
	}

	pm.batteryStatus.Status = power_manager.Status_Ok

	if oldStatus != pm.batteryStatus {
		// Only update time stamp when status changes
		pm.batteryStatus.Timestamp = time.Now().UnixNano()

		// uncomment for debugging
		// logger.Printf("Battery status changed from %v to %v", oldStatus, pm.batteryStatus)
		for _, pmw := range pm.watchers {
			go pmw.OnChangeBatteryStatus(pm.batteryStatus)
		}
	}

	return nil
}

func (pm *PowerManager) Bind(r power_manager.PowerManager_Request) {
	logger.Println("Bind")

	s := r.NewStub(pm, bindings.GetAsyncWaiter())

	go func() {
		defer logger.Println("Bye Bind")
		for {
			if err := s.ServeRequest(); err != nil {
				if mxerror.Status(err) != mx.ErrPeerClosed {
					log.Println(err)
				}
				break
			}
		}
	}()
}

func main() {
	logger.Println("start")
	defer logger.Println("stop")
	watcher, err := NewWatcher(powerDevice)
	if err != nil {
		logger.Printf("Error while watching device %q: %s\n", powerDevice, err)
		return
	}
	now := time.Now().UnixNano()
	pm := &PowerManager{
		batteryStatus: power_manager.BatteryStatus{
			Status:    power_manager.Status_NotAvailable,
			Level:     float32(0),
			Timestamp: now,
		},
		batteryStatusTimeStamp: now,
		powerAdapterTimeStamp:  now,
	}

	c := context.CreateFromStartupInfo()
	c.OutgoingService.AddService(&power_manager.PowerManager_ServiceBinder{pm})
	c.Serve()

	adapterDeviceFound := false
	batteryDeviceFound := false
	for file := range watcher.C {
		f := filepath.Join(powerDevice, file)
		m, err := syscall.OpenPath(f, syscall.O_RDONLY, 0)
		if err != nil {
			logger.Printf("Error while opening device %q: %s\n", f, err)
			return
		}

		pi, err := getPowerInfo(m)
		if err != nil {
			logger.Printf("Failed to get power info from %q: %s\n", f, err)
			return
		}
		if pi.PowerType == 1 && batteryDeviceFound {
			logger.Println("Skip %q as battery device already found", f)
			continue
		} else if pi.PowerType == 0 && adapterDeviceFound {
			logger.Println("Skip %q as adapter device already found", f)
			continue
		}

		if err := addListener(m, func(m fdio.FDIO) {
			if err := pm.updateStatus(m); err != nil {
				logger.Printf("Error while updating battery status: %s", err)
			}
		}); err != nil {
			logger.Printf("Not able to add listener to %q: %s\n", f, err)
		}

		if err := pm.updateStatus(m); err != nil {
			logger.Printf("Error while updating battery status: %s", err)
		}

		if pi.PowerType == 1 {
			batteryDeviceFound = true
			go func(m fdio.FDIO) {
				for {
					time.Sleep(time.Duration(updateWaitTimeFlag) * time.Second)
					if err := pm.updateStatus(m); err != nil {
						logger.Printf("Error while updating battery status: %s", err)
					}
				}
			}(m)
		} else {
			adapterDeviceFound = true
		}

		if batteryDeviceFound && adapterDeviceFound {
			watcher.Stop()
			break
		}
	}
	select {}
}
