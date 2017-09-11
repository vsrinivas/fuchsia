// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"app/context"
	"bufio"
	"fidl/bindings"
	"flag"
	"log"
	"os"
	"regexp"
	"strconv"
	"sync"
	"syscall/mx"
	"syscall/mx/mxerror"
	"time"

	"garnet/public/lib/power/fidl/power_manager"
)

var (
	updateWaitTimeFlag uint
	logger             = log.New(os.Stdout, "power_manager: ", log.Lshortfile)
	re                 = regexp.MustCompile("^(c?)([0-9][0-9]?[0-9]?)%$")
)

func init() {
	// TODO: Make is interrupt based, tracking BUG: MG-996.
	flag.UintVar(&updateWaitTimeFlag, "update-wait-time", 60, "Time to sleep between status update in seconds.")
}

type PowerManager struct {
	mu             sync.Mutex
	battery_status power_manager.BatteryStatus
	watchers       []*power_manager.PowerManagerWatcher_Proxy
}

func (pm *PowerManager) GetBatteryStatus() (power_manager.BatteryStatus, error) {
	logger.Println("GetBatteryStatus")
	pm.mu.Lock()
	defer pm.mu.Unlock()

	return pm.battery_status, nil
}

func (pm *PowerManager) Watch(watcher power_manager.PowerManagerWatcher_Pointer) error {
	pmw := power_manager.NewProxyForPowerManagerWatcher(watcher, bindings.GetAsyncWaiter())
	pm.mu.Lock()
	pm.watchers = append(pm.watchers, pmw)
	pm.mu.Unlock()
	go pmw.OnChangeBatteryStatus(pm.battery_status)
	return nil
}

// Updates the status and returns false if battery status
// cannot be updated in future.
func (pm *PowerManager) updateStatus() bool {

	f, err := os.Open("/dev/class/battery/000")
	if err != nil {
		logger.Printf("Error while getting status: %s\n", err)
		return false
	}
	defer f.Close()
	r := bufio.NewReader(f)
	b := make([]byte, 10)
	n, err := r.Read(b)
	if err != nil {
		logger.Printf("Warning: Error while getting status: %s\n", err)
		// Not fatal
		return true
	}
	if n == 0 {
		logger.Printf("Warning: Battery status format blank.")
		// Not fatal
		return true
	}

	m := re.FindStringSubmatch(string(b[:n-1]))
	if len(m) != 3 {
		logger.Printf("Warning: Battery status format wrong, text: %q, submatch: %v\n", string(b[:n-1]), m)
		// Not fatal
		return true
	}

	pm.mu.Lock()
	defer pm.mu.Unlock()
	oldStatus := pm.battery_status

	pm.battery_status.Charging = m[1] == "c"
	i, _ := strconv.Atoi(m[2])
	pm.battery_status.Level = uint16(i)
	pm.battery_status.Status = power_manager.Status_Ok

	if oldStatus != pm.battery_status {
		logger.Printf("Battery status changed from %v to %v", oldStatus, pm.battery_status)
		for _, pmw := range pm.watchers {
			go pmw.OnChangeBatteryStatus(pm.battery_status)
		}
	}

	return true
}

func (pm *PowerManager) Bind(r power_manager.PowerManager_Request) {
	logger.Println("Bind")

	s := r.NewStub(pm, bindings.GetAsyncWaiter())

	go func() {
		defer logger.Println("bye Bind")
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

	watcher, err := NewWatcher("/dev/class/battery")
	if err != nil {
		logger.Printf("Error while watching device: %s\n", err)
		return
	}
	pm := &PowerManager{
		battery_status: power_manager.BatteryStatus{
			Status:   power_manager.Status_NotAvailable,
			Level:    uint16(0),
			Charging: false,
		},
	}

	c := context.CreateFromStartupInfo()
	c.OutgoingService.AddService(&power_manager.PowerManager_ServiceBinder{pm})
	c.Serve()

	for file := range watcher.C {
		if file == "000" {
			// File found, get status
			watcher.Stop()
			break
		}
	}

	go func() {
		logger.Println("update status")
		for {
			if !pm.updateStatus() {
				break
			}
			time.Sleep(time.Duration(updateWaitTimeFlag) * time.Second)
		}
	}()

	select {}
}
