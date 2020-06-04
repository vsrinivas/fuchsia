package main

import (
	"flag"

	"go.fuchsia.dev/fuchsia/scripts/check-licenses/lib"
)

func main() {
	configJson := flag.String("config", "tools/check-licenses/config/config.json", "Location of config.json")
	var config lib.Config
	config.Init(configJson)
	lib.Walk(&config)
}
