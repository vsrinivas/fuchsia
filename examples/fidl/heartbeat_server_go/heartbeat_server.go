package main

import (
	"fmt"
	"os"
	"time"

	"app/context"
	"fidl/bindings"

	"syscall/zx"

	"fidl/fidl/examples/heartbeat"
)

type heartbeatImpl struct{}

func main() {
	quiet := (len(os.Args) > 1) && os.Args[1] == "-q"
	heartbeatService := &heartbeat.HeartbeatService{}
	c := context.CreateFromStartupInfo()
	c.OutgoingService.AddService(heartbeat.HeartbeatName, func(c zx.Channel) error {
		_, err := heartbeatService.Add(&heartbeatImpl{}, c, nil)
		return err
	})
	c.Serve()
	go bindings.Serve()

	go func() {
		for {
			time.Sleep(1 * time.Second)
			for key, _ := range heartbeatService.Bindings {
				if p, ok := heartbeatService.EventProxyFor(key); ok {
					if !quiet {
						fmt.Println("heartbeat server: sending heartbeat")
					}
					p.Heartbeat(false)
				}
			}
		}
	}()

	select {}
}
