// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package amberd

import (
	"flag"
	"fmt"
	"log"
	"runtime"
	"syscall/zx"
	"syscall/zx/fidl"

	"amber/control_server"
	"amber/daemon"
	"amber/metrics"
	"amber/source"

	"fidl/fuchsia/amber"

	"app/context"
	"syslog"
)

func Main() {

	var (
		usage = "usage: amber"
	)

	flag.CommandLine.Usage = func() {
		fmt.Println(usage)
		flag.CommandLine.PrintDefaults()
	}

	ctx := context.CreateFromStartupInfo()

	{
		if l, err := syslog.NewLoggerWithDefaults(ctx.Connector(), "amber"); err != nil {
			log.Println(err)
		} else {
			syslog.SetDefaultLogger(l)
			log.SetOutput(&syslog.Writer{Logger: l})
		}
		log.SetFlags(0)
	}

	metrics.Register(ctx)

	flag.Parse()

	var ctlSvc amber.ControlService
	d, err := daemon.NewDaemon(source.PkgfsDir{"/pkgfs"})
	if err != nil {
		log.Fatalf("failed to start daemon: %s", err)
	}

	ctx.OutgoingService.AddService(
		amber.ControlName,
		&amber.ControlStub{Impl: control_server.NewControlServer(d)},
		func(s fidl.Stub, c zx.Channel) error {
			_, err := ctlSvc.BindingSet.Add(s, c, nil)
			return err
		},
	)

	for i := 1; i < runtime.NumCPU(); i++ {
		go fidl.Serve()
	}
	fidl.Serve()
}
