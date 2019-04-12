package metrics

import (
	"app/context"
	"errors"
	"fmt"
	"log"
	"time"

	cobalt "fidl/fuchsia/cobalt"
)

type releaseChannelUpdater struct {
	in  chan string
	ctx *context.Context
}

func startReleaseChannelUpdater(ctx *context.Context) chan string {
	in := make(chan string)
	u := releaseChannelUpdater{in: in, ctx: ctx}
	go u.run()
	return in
}

func (u *releaseChannelUpdater) run() {
	var sendResult chan error
	var lastSent *string
	var pendingSend *string
	for {
		var toSend *string
		select {
		case err := <-sendResult:
			sendResult = nil
			if err != nil {
				log.Printf("error sending release channel to cobalt: %v", err)
				// will want to retry
				toSend = lastSent
			}
			if pendingSend != nil {
				toSend = pendingSend
				pendingSend = nil
			}
		case t := <-u.in:
			if sendResult == nil {
				// Can send immediately
				toSend = &t
			} else {
				pendingSend = &t
			}
		}
		if toSend != nil {
			var fatal error
			sendResult, fatal = u.sendAsync(*toSend)
			if fatal != nil {
				log.Printf("fatal error sending release channel to cobalt: %v; will no longer send release channel updates to cobalt", fatal)
				break
			}
			lastSent = toSend
		}
	}
	// We only get here if something went really wrong
	for {
		t := <-u.in
		log.Printf("not sending channel update to cobalt due to a previous error. New channel: %s", t)
	}
}

func (u *releaseChannelUpdater) sendAsync(targetChannel string) (result chan error, fatal error) {
	defer func() {
		if r := recover(); r != nil {
			fatal = fmt.Errorf("%v", r)
		}
	}()
	r, proxy, err := cobalt.NewSystemDataUpdaterInterfaceRequest()
	if err != nil {
		return nil, err
	}
	ctx.ConnectToEnvService(r)
	result = make(chan error)
	go func() {
		defer proxy.Close()
		result <- u.sendSync(proxy, targetChannel)
	}()
	return
}

func (u *releaseChannelUpdater) sendSync(proxy *cobalt.SystemDataUpdaterInterface, targetChannel string) error {
	log.Printf("calling cobalt.SetChannel(nil, %q)", targetChannel)
	result, err := proxy.SetChannel(nil, &targetChannel)
	if err != nil {
		time.Sleep(5 * time.Second)
		// channel broken, return error so we try to reconnect
		return err
	}
	if result == cobalt.StatusEventTooBig {
		time.Sleep(5 * time.Second)
		// Return an error so we retry
		return errors.New("cobalt.SetChannel returned Status.EVENT_TOO_BIG")
	}
	if result != cobalt.StatusOk {
		// Not much we can do about the other status codes but log.
		log.Printf("cobalt.SetChannel returned non-OK status: %v", result)
	}
	return nil
}
