package metrics

import (
	"encoding/json"
	"log"
	"os"
)

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
