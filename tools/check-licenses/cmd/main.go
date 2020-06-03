package main

import (
	licenses "go.fuchsia.dev/fuchsia/scripts/check-licenses/lib"
)

func main() {
	config := licenses.Config{}
	licenses.Walk(".", config)
}
