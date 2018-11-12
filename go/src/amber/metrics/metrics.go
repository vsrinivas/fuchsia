package metrics

import (
	"fidl/fuchsia/cobalt"
	"time"
)

type metricID uint32

const (
	_ metricID = iota
	metricSystemUpToDate
	metricOtaStart
)

// Log synchronously submits the given metric to cobalt
func Log(metric Metric) {
	metric.log()
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
