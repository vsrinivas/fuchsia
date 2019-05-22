package metrics

import (
	"app/context"
	"fmt"
	"log"
	"strings"
	"time"

	"fidl/fuchsia/cobalt"
)

var (
	ctx              *context.Context
	logger           cobalt.Logger
	setTargetChannel chan string
)

// Register uses an app context to connect to the cobalt service and configure the sw_delivery project
func Register(c *context.Context) {
	ctx = c
	ensureConnection()
	setTargetChannel = startReleaseChannelUpdater(ctx)
}

type errCobalt cobalt.Status

func mapErrCobalt(status cobalt.Status) error {
	if status == cobalt.StatusOk {
		return nil
	}
	return errCobalt(status)
}

func (e errCobalt) Error() string {
	switch cobalt.Status(e) {
	case cobalt.StatusInvalidArguments:
		return "cobalt: invalid arguments"
	case cobalt.StatusEventTooBig:
		return "cobalt: event too big"
	case cobalt.StatusBufferFull:
		return "cobalt: buffer full"
	case cobalt.StatusInternalError:
		return "cobalt: internal error"
	}
	return fmt.Sprintf("cobalt: unspecified error (%d)", e)
}

func newStringValue(name string, value string) cobalt.CustomEventValue {
	var val cobalt.Value
	val.SetStringValue(value)
	return cobalt.CustomEventValue{
		DimensionName: name,
		Value:         val,
	}
}

func newIndexValue(name string, value uint32) cobalt.CustomEventValue {
	var val cobalt.Value
	val.SetIndexValue(value)
	return cobalt.CustomEventValue{
		DimensionName: name,
		Value:         val,
	}
}

func newIntValue(name string, value int64) cobalt.CustomEventValue {
	var val cobalt.Value
	val.SetIntValue(value)
	return cobalt.CustomEventValue{
		DimensionName: name,
		Value:         val,
	}
}

func newDoubleValue(name string, value float64) cobalt.CustomEventValue {
	var val cobalt.Value
	val.SetDoubleValue(value)
	return cobalt.CustomEventValue{
		DimensionName: name,
		Value:         val,
	}
}

func ensureConnection() bool {
	// Note(rudominer) Cobalt support for legacy 0.1 projects has been
	// removed so we cannot log to Cobalt using Amber's old Cobalt 0.1 project
	// or Cobalt will return an error. The following CL will move Amber to a new
	// Cobalt 1.0 project:
	// https://fuchsia-review.googlesource.com/c/fuchsia/+/272921.
	// Until that CL can be submitted, we disable logging to Cobalt.
	logger = nil
	return false
}

func logString(metric metricID, value string) {
	if !ensureConnection() {
		return
	}

	status, err := logger.LogString(uint32(metric), value)
	if err != nil {
		log.Printf("logString: %s", err)
	} else if err := mapErrCobalt(status); err != nil {
		log.Printf("logString: %s", err)
	}
}

func logElapsedTime(metric metricID, duration time.Duration, index uint32, component string) {
	if !ensureConnection() {
		return
	}

	durationInMicroseconds := duration.Nanoseconds() / time.Microsecond.Nanoseconds()

	status, err := logger.LogElapsedTime(uint32(metric), index, component, durationInMicroseconds)
	if err != nil {
		log.Printf("logElapsedTime: %s", err)
	} else if err := mapErrCobalt(status); err != nil {
		log.Printf("logElapsedTime: %s", err)
	}
}

func valueString(value cobalt.Value) (string, string) {
	switch value.ValueTag {
	case cobalt.ValueStringValue:
		return "STRING", value.StringValue
	case cobalt.ValueIntValue:
		return "INT", fmt.Sprintf("%d", value.IntValue)
	case cobalt.ValueDoubleValue:
		return "DOUBLE", fmt.Sprintf("%f", value.DoubleValue)
	case cobalt.ValueIndexValue:
		return "INDEX", fmt.Sprintf("%d", value.IndexValue)
	}
	return "UNKNOWN", "UNKNOWN"
}

func logCustomEvent(metric metricID, parts []cobalt.CustomEventValue) {
	if !ensureConnection() {
		return
	}

	partStrings := make([]string, 0, len(parts))
	for _, part := range parts {
		t, v := valueString(part.Value)
		partStrings = append(partStrings, fmt.Sprintf(" - %s: %s = %v", part.DimensionName, t, v))
	}
	log.Printf("logCustomEvent(%d)\n%s", uint32(metric), strings.Join(partStrings, "\n"))

	status, err := logger.LogCustomEvent(uint32(metric), parts)
	if err != nil {
		log.Printf("logCustomEvent: %s", err)
	} else if err := mapErrCobalt(status); err != nil {
		log.Printf("logCustomEvent: %s", err)
	}
}
