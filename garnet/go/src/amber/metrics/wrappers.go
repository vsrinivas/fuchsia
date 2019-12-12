package metrics

import (
	"app/context"
	"fmt"
	"log"
	"strings"
	"time"

	"fidl/fuchsia/cobalt"
)

const (
	// This means that this code is considered GA (Generally Available) and so it
	// will not be allowed to use any Cobalt metrics that are marked as being only
	// for "DEBUG" or "FISHFOOD" etc.
	releaseStage = cobalt.ReleaseStageGa
	// This must match the name of our project as specified in Cobalt's metrics registry:
	// https://cobalt-analytics.googlesource.com/config/+/refs/heads/master/projects.yaml
	projectName = "software_delivery"
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

func connect() error {

	factoryRequest, factory, err := cobalt.NewLoggerFactoryInterfaceRequest()
	if err != nil {
		return err
	}
	ctx.ConnectToEnvService(factoryRequest)
	defer factory.Close()

	loggerRequest, proxy, err := cobalt.NewLoggerInterfaceRequest()
	if err != nil {
		return err
	}

	status, err := factory.CreateLoggerFromProjectName(projectName, releaseStage, loggerRequest)
	if err != nil {
		proxy.Close()
		return err
	} else if err := mapErrCobalt(status); err != nil {
		proxy.Close()
		return err
	}

	logger = proxy

	return nil
}

func ensureConnection() bool {
	if logger == nil {
		if err := connect(); err != nil {
			log.Printf("connect to cobalt: %s", err)
		}
	}
	return logger != nil
}

// logEventMulti() invokes logEventCountMulti() using periodDurationMicros=0
// and count=1. Cobalt's simple EVENT_OCCURRED metric type does not support
// multiple dimensions of event codes or a component string. When one wants to
// use these features one is supposed to use the EVENT_COUNT metric type,
// always setting the count=1 and the duration=0.
func logEventMulti(metric metricID, eventCodes []uint32, component string) {
	logEventCountMulti(metric, 0, 1, eventCodes, component)
}

func logEventCountMulti(metric metricID, periodDurationMicros int64, count int64,
	eventCodes []uint32, component string) {
	if !ensureConnection() {
		return
	}

	var eventPayload cobalt.EventPayload
	eventPayload.SetEventCount(cobalt.CountEvent{
		PeriodDurationMicros: periodDurationMicros,
		Count:                count,
	})

	event := cobalt.CobaltEvent{
		MetricId:   uint32(metric),
		EventCodes: eventCodes,
		Component:  &component,
		Payload:    eventPayload,
	}
	status, err := logger.LogCobaltEvent(event)
	if err != nil {
		log.Printf("logEventCountMulti: %s", err)
	} else if err := mapErrCobalt(status); err != nil {
		log.Printf("logEventCountMulti: %s", err)
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

func logElapsedTimeMulti(metric metricID, duration time.Duration, eventCodes []uint32, component string) {
	if !ensureConnection() {
		return
	}

	var eventPayload cobalt.EventPayload
	eventPayload.SetElapsedMicros(duration.Nanoseconds() / time.Microsecond.Nanoseconds())

	event := cobalt.CobaltEvent{
		MetricId:   uint32(metric),
		EventCodes: eventCodes,
		Component:  &component,
		Payload:    eventPayload,
	}
	status, err := logger.LogCobaltEvent(event)
	if err != nil {
		log.Printf("logElapsedTimeMulti: %s", err)
	} else if err := mapErrCobalt(status); err != nil {
		log.Printf("logElapsedTimeMulti: %s", err)
	}
}

func valueString(value cobalt.Value) (string, string) {
	switch value.Which() {
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
