package metrics

import (
	"app/context"
	"fmt"
	"io/ioutil"
	"log"
	"strings"
	"syscall/zx"
	"time"

	"fidl/fuchsia/cobalt"
	"fidl/fuchsia/mem"
)

const (
	metricConfigPath = "/pkg/data/cobalt_config.pb"
	releaseStage     = cobalt.ReleaseStageDebug
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
	config, err := ioutil.ReadFile(metricConfigPath)
	if err != nil {
		return fmt.Errorf("read cobalt config: %s", err)
	}

	configVmo, err := zx.NewVMO(uint64(len(config)), 0)
	if err != nil {
		return err
	}
	if err := configVmo.Write(config, 0); err != nil {
		configVmo.Close()
		return fmt.Errorf("write cobalt vmo: %s", err)
	}

	factoryRequest, factory, err := cobalt.NewLoggerFactoryInterfaceRequest()
	if err != nil {
		configVmo.Close()
		return err
	}
	ctx.ConnectToEnvService(factoryRequest)
	defer factory.Close()

	loggerRequest, proxy, err := cobalt.NewLoggerInterfaceRequest()
	if err != nil {
		configVmo.Close()
		return err
	}

	profile := cobalt.ProjectProfile{
		Config: mem.Buffer{
			Vmo:  configVmo,
			Size: uint64(len(config)),
		},
		ReleaseStage: releaseStage,
	}

	status, err := factory.CreateLogger(profile, loggerRequest)
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
