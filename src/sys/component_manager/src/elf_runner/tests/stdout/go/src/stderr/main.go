package main

import "fmt"
import "os"

func main() {
	fmt.Fprintln(os.Stderr, "Hello Stderr!")
}
