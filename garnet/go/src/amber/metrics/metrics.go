package metrics

import (
	"log"
	"time"

	"fidl/fuchsia/cobalt"
)

type metricID uint32

const (
	_ metricID = iota
	metricSystemUpToDate
	metricOtaStart
	metricOtaResult
)

// Log synchronously submits the given metric to cobalt
func Log(metric Metric) {
	metric.log()
}

// SetTargetChannel asynchronously tells cobalt which channel is configured for updates.
func SetTargetChannel(targetChannel string) {
	if setTargetChannel == nil {
		log.Print("SetTargetChannel() called before metrics.Register(), release channel info won't be sent to cobalt.")
		return
	}
	setTargetChannel <- targetChannel
}

// Metric is a cobalt metric that can be submitted to cobalt
type Metric interface {
	log()
}

type SystemUpToDate struct {
	Initiator Initiator
	Version   string
	When      time.Time
}

func (m SystemUpToDate) log() {
	logCustomEvent(metricSystemUpToDate, []cobalt.CustomEventValue{
		newIndexValue("initiator", uint32(m.Initiator)),
		newStringValue("version", m.Version),
		newIndexValue("when", uint32(m.When.Local().Hour())),
	})
}

type OtaStart struct {
	Initiator Initiator
	Source    string
	Target    string
	When      time.Time
}

func (m OtaStart) log() {
	logCustomEvent(metricOtaStart, []cobalt.CustomEventValue{
		newIndexValue("initiator", uint32(m.Initiator)),
		newStringValue("source", m.Source),
		newStringValue("target", m.Target),
		newIndexValue("when", uint32(m.When.Local().Hour())),
	})
}

type OtaResult struct {
	Initiator      Initiator
	Source         string
	Target         string
	Attempt        int64
	FreeSpaceStart int64
	FreeSpaceEnd   int64
	When           time.Time
	Duration       time.Duration
	Phase          Phase
	ErrorSource    string
	ErrorText      string
	ErrorCode      int64
}

func (m OtaResult) log() {
	durationInMilliseconds := m.Duration.Nanoseconds() / time.Millisecond.Nanoseconds()

	logCustomEvent(metricOtaResult, []cobalt.CustomEventValue{
		newIndexValue("initiator", uint32(m.Initiator)),
		newStringValue("source", m.Source),
		newStringValue("target", m.Target),
		newIntValue("attempt", m.Attempt),
		newIntValue("free_space_start", m.FreeSpaceStart),
		newIntValue("free_space_end", m.FreeSpaceEnd),
		newIndexValue("when", uint32(m.When.Local().Hour())),
		newIntValue("duration", durationInMilliseconds),
		newIndexValue("phase", uint32(m.Phase)),
		newStringValue("error_source", m.ErrorSource),
		newStringValue("error_text", m.ErrorText),
		newIntValue("error_code", m.ErrorCode),
	})
}
