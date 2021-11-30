package main

import "fmt"
import "os"

func main() {
	fmt.Fprintln(os.Stdout, "Hello Stdout!")
	fmt.Fprintln(os.Stderr, "Hello Stderr!")
}
