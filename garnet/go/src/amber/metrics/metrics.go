package metrics

import (
	"log"
	"time"
)

type metricID uint32

const (
	_ metricID = iota
	metricSystemUpToDate
	metricOtaStart
	metricOtaResultAttempt
	metricOtaResultDuration
	metricOtaResultFreeSpaceDelta
)

type Status uint32

const (
	// The names and integer values must correspond to the event_codes in the
	// |status_codes| dimension of the metrics in our Cobalt registry at
	// https://cobalt-analytics.googlesource.com/config/+/refs/heads/master/fuchsia/software_delivery/config.yaml
	StatusSuccess Status = iota
	StatusFailure
)

func StatusFromError(err error) Status {
	if err == nil {
		return StatusSuccess
	}
	return StatusFailure
}

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
	writeTargetChannel(targetChannel)
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
	logEventMulti(metricSystemUpToDate, []uint32{uint32(m.Initiator), uint32(m.When.Local().Hour())}, "")
}

type OtaStart struct {
	Initiator Initiator
	Target    string
	When      time.Time
}

func (m OtaStart) log() {
	logEventMulti(metricOtaStart, []uint32{uint32(m.Initiator), uint32(m.When.Local().Hour())}, m.Target)
}

type OtaResultAttempt struct {
	Initiator Initiator
	Target    string
	Attempt   int64
	Phase     Phase
	Status    Status
}

func (m OtaResultAttempt) log() {
	logEventCountMulti(metricOtaResultAttempt, 0, m.Attempt,
		[]uint32{uint32(m.Initiator), uint32(m.Phase), uint32(m.Status)},
		m.Target)
}

type OtaResultDuration struct {
	Initiator Initiator
	Target    string
	Duration  time.Duration
	Phase     Phase
	Status    Status
}

func (m OtaResultDuration) log() {
	logElapsedTimeMulti(metricOtaResultDuration, m.Duration,
		[]uint32{uint32(m.Initiator), uint32(m.Phase), uint32(m.Status)},
		m.Target)
}

type OtaResultFreeSpaceDelta struct {
	Initiator      Initiator
	Target         string
	FreeSpaceDelta int64
	Duration       time.Duration
	Phase          Phase
	Status         Status
}

func (m OtaResultFreeSpaceDelta) log() {
	logEventCountMulti(metricOtaResultFreeSpaceDelta, 0, m.FreeSpaceDelta,
		[]uint32{uint32(m.Initiator), uint32(m.Phase), uint32(m.Status)},
		m.Target)
}
