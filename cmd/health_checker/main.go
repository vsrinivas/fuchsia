// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"os/exec"
	"time"

	"fuchsia.googlesource.com/tools/botanist"
	"fuchsia.googlesource.com/tools/netboot"
)

const usage = `usage: health_checker [options]

Checks the health of the attached device by checking to see if it can
discover and ping the device's netsvc address. A healthy device should be
running in Zedboot.
`

// Command line flag values
var (
	timeout           time.Duration
	configFile        string
	rebootIfUnhealthy bool
)

const (
	healthyState   = "healthy"
	unhealthyState = "unhealthy"
)

// DeviceHealthProperties contains health properties of a hardware device.
type HealthCheckResult struct {
	// Nodename is the hostname of the device that we want to boot on.
	Nodename string `json:"nodename"`

	// State is the health status of the device (either "healthy" or "unhealthy").
	State string `json:"state"`

	// ErrorMsg is the error message provided by the health check.
	ErrorMsg string `json:"error_msg"`
}

func checkHealth(n *netboot.Client, nodename string) HealthCheckResult {
	netsvcAddr, err := n.Discover(nodename, false)
	if err != nil {
		err = fmt.Errorf("Failed to discover netsvc addr: %v.", err)
		return HealthCheckResult{nodename, unhealthyState, err.Error()}
	}
	netsvcIpAddr := &net.IPAddr{IP: netsvcAddr.IP, Zone: netsvcAddr.Zone}
	cmd := exec.Command("ping", "-6", netsvcIpAddr.String(), "-c", "1")
	if _, err = cmd.Output(); err != nil {
		err = fmt.Errorf("Failed to ping netsvc addr %s: %v.", netsvcIpAddr, err)
		return HealthCheckResult{nodename, unhealthyState, err.Error()}
	}

	// Device should be in Zedboot, so fuchsia address should be unpingable
	fuchsiaAddr, err := n.Discover(nodename, true)
	if err != nil {
		err = fmt.Errorf("Failed to discover fuchsia addr: %v.", err)
		return HealthCheckResult{nodename, unhealthyState, err.Error()}
	}
	fuchsiaIpAddr := &net.IPAddr{IP: fuchsiaAddr.IP, Zone: fuchsiaAddr.Zone}
	cmd = exec.Command("ping", "-6", fuchsiaIpAddr.String(), "-c", "1")
	if _, err = cmd.Output(); err == nil {
		return HealthCheckResult{nodename, unhealthyState, "Device is in Fuchsia, should be in Zedboot."}
	}
	return HealthCheckResult{nodename, healthyState, ""}
}

func reboot(properties botanist.DeviceProperties) error {
	if properties.PDU == nil {
		return fmt.Errorf("Failed to reboot the device: missing PDU info in botanist config file.")
	}
	signers, err := botanist.SSHSignersFromDeviceProperties([]botanist.DeviceProperties{properties})
	if err != nil {
		return fmt.Errorf("Failed to reboot the device: %v.", err)
	}

	if err = botanist.RebootDevice(properties.PDU, signers, properties.Nodename); err != nil {
		return fmt.Errorf("Failed to reboot the device: %v.", err)
	}
	return nil
}

func printHealthCheckResults(checkResults []HealthCheckResult) error {
	output, err := json.Marshal(checkResults)
	if err != nil {
		return err
	}
	fmt.Println(string(output))
	return nil
}

func init() {
	flag.Usage = func() {
		fmt.Fprint(os.Stderr, usage)
		flag.PrintDefaults()
	}

	// First set the flags ...
	flag.StringVar(&configFile, "config", "/etc/botanist/config.json",
		"The path of the json config file that contains the nodename of the device. Format is defined in https://fuchsia.googlesource.com/tools/+/master/botanist/common.go")
	flag.DurationVar(&timeout, "timeout", 10*time.Second,
		"The timeout for checking each device. The format should be a value acceptable to time.ParseDuration.")
	flag.BoolVar(&rebootIfUnhealthy, "reboot", false, "If true, attempt to reboot the device if unhealthy.")
}

func main() {
	flag.Parse()
	client := netboot.NewClient(timeout)
	var devices []botanist.DeviceProperties
	devices, err := botanist.LoadDeviceProperties(configFile)
	if err != nil {
		log.Fatal(err)
	}
	var checkResultSlice []HealthCheckResult
	for _, deviceProperties := range devices {
		nodename := deviceProperties.Nodename
		if nodename == "" {
			log.Fatal("Failed to retrieve nodename from config file")
		}
		checkResult := checkHealth(client, nodename)
		if checkResult.State == unhealthyState && rebootIfUnhealthy {
			if rebootErr := reboot(deviceProperties); rebootErr != nil {
				checkResult.ErrorMsg += "; " + rebootErr.Error()
			}
		}
		checkResultSlice = append(checkResultSlice, checkResult)
	}
	if err = printHealthCheckResults(checkResultSlice); err != nil {
		log.Fatal(err)
	}
}
