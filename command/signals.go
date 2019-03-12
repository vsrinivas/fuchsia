package command

import (
	"context"
	"os"
	"os/signal"
)

// CancelOnSignals returns a Context that emits a Done event when any of the input signals
// are recieved, assuming those signals can be handled by the current process.
func CancelOnSignals(ctx context.Context, sigs ...os.Signal) context.Context {
	ctx, cancel := context.WithCancel(ctx)
	signals := make(chan os.Signal)
	signal.Notify(signals, sigs...)
	go func() {
		select {
		case s := <-signals:
			if s != nil {
				cancel()
				close(signals)
			}
		}
	}()
	return ctx
}
