// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"

	"app/context"
	"svc/services"

	"fidl/fidl/examples/heartbeat"
	"fidl/fuchsia/sys"
)

type heartbeatClientApp struct {
	ctx               *context.Context
	heartbeatProvider *services.Provider
	controller        *sys.ComponentControllerInterface
	heartbeat         *heartbeat.HeartbeatInterface
}

func (a *heartbeatClientApp) startApplication(serverURL string) (li *sys.ComponentControllerInterface, err error) {
	pr, err := a.heartbeatProvider.NewRequest()
	if err != nil {
		return nil, fmt.Errorf("NewRequest failed: %v", err)
	}
	defer func() {
		if err != nil {
			pr.Close()
		}
	}()

	launchInfo := sys.LaunchInfo{
		Url:              serverURL,
		DirectoryRequest: pr,
	}

	cr, cp, err := sys.NewComponentControllerInterfaceRequest()
	if err != nil {
		return nil, fmt.Errorf("NewComponentControllerInterfaceRequest failed: %v", err)
	}
	defer func() {
		if err != nil {
			cr.Close()
			cp.Close()
		}
	}()

	err = a.ctx.Launcher.CreateComponent(launchInfo, cr)
	if err != nil {
		return nil, fmt.Errorf("CreateComponent failed: %v", err)
	}
	return cp, nil
}

func (a *heartbeatClientApp) getHeartbeatInterface() (ei *heartbeat.HeartbeatInterface, err error) {
	r, p, err := heartbeat.NewHeartbeatInterfaceRequest()
	if err != nil {
		return nil, fmt.Errorf("NewHeartbeatInterfaceRequest failed: %v", err)
	}
	defer func() {
		if err != nil {
			r.Close()
			p.Close()
		}
	}()

	err = a.heartbeatProvider.ConnectToService(r)
	if err != nil {
		return nil, fmt.Errorf("ConnectToServiceAt failed: %v", err)
	}
	return p, nil
}

func main() {
	serverURL := flag.String("server", "heartbeat_server_go", "server URL")

	flag.Parse()

	a := &heartbeatClientApp{
		ctx:               context.CreateFromStartupInfo(),
		heartbeatProvider: services.NewProvider(),
	}

	var err error
	a.controller, err = a.startApplication(*serverURL)
	if err != nil {
		fmt.Println(err)
		return
	}
	defer a.controller.Close()

	a.heartbeat, err = a.getHeartbeatInterface()
	if err != nil {
		fmt.Println(err)
		return
	}
	defer a.heartbeat.Close()

	done := make(chan struct{}, 1)
	go func() {
		for {
			shuttingDown, err := a.heartbeat.ExpectHeartbeat()
			if err != nil {
				fmt.Println("waiting for heartbeat failed:", err)
				return
			}

			if shuttingDown {
				fmt.Println("server shutting down; shutting down.")
				done <- struct{}{}
				break
			}

			fmt.Println("heartbeat client: received heartbeat")
		}
	}()

	<-done
}
