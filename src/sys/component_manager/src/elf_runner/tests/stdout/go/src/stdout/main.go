package main

import "fmt"
import "os"
import "time"

func main() {
	fmt.Fprintln(os.Stdout, "Hello Stdout!")

	// TODO(https://fxbug.dev/95602) delete this when clean shutdown works
	time.Sleep(time.Duration(1<<63 - 1))
}
