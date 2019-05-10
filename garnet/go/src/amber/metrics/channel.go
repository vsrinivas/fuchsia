package metrics

import (
	"app/context"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"os"
	"time"

	cobalt "fidl/fuchsia/cobalt"
)

type releaseChannelUpdater struct {
	in             chan string
	ctx            *context.Context
	currentChannel *string
}

func startReleaseChannelUpdater(ctx *context.Context) chan string {
	cc := readCurrentChannel()
	in := make(chan string)
	u := releaseChannelUpdater{in: in, ctx: ctx, currentChannel: cc}
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
	var result cobalt.Status
	var err error
	if u.currentChannel == nil {
		log.Printf("calling cobalt.SetChannel(\"\")")
		result, err = proxy.SetChannel("")
	} else {
		log.Printf("calling cobalt.SetChannel(%q)", *u.currentChannel)
		result, err = proxy.SetChannel(*u.currentChannel)
	}

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

type channelMetadataValue struct {
	ChannelName string `json:"legacy_amber_source_name"`
}

type channelMetadataFile struct {
	Version string               `json:"version"`
	Content channelMetadataValue `json:"content"`
}

func readCurrentChannel() *string {
	path := "/misc/ota/current_channel.json"
	f, err := os.Open(path)
	if err != nil {
		log.Printf("error opening %v: %v", path, err)
		return nil
	}
	defer f.Close()
	v := channelMetadataFile{}
	err = json.NewDecoder(f).Decode(&v)
	if err != nil {
		log.Printf("error decoding %v: %v", path, err)
		return nil
	}
	if v.Version != "1" {
		log.Printf("unsupported %v version: %v", path, v.Version)
		return nil
	}
	result := v.Content.ChannelName
	return &result
}

func writeTargetChannel(targetChannel string) {
	err := os.MkdirAll("/misc/ota/", 0700)
	if err != nil {
		log.Printf("error creating /misc/ota/ directory...: %v", err)
		return
	}
	path := "/misc/ota/target_channel.json"
	partPath := path + ".part"
	f, err := os.Create(partPath)
	if err != nil {
		log.Printf("error creating %v: %v", partPath, err)
		return
	}
	defer f.Close()
	v := channelMetadataFile{
		Version: "1",
		Content: channelMetadataValue{
			ChannelName: targetChannel,
		},
	}
	if err := json.NewEncoder(f).Encode(&v); err != nil {
		log.Printf("error writing %v: %v", path, err)
		return
	}
	f.Sync()
	f.Close()
	if err := os.Rename(partPath, path); err != nil {
		log.Printf("error moving %v to %v: %v", partPath, path, err)
		return
	}
}
