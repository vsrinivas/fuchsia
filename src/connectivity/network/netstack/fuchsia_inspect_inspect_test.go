package netstack

import (
	"reflect"
	"testing"

	inspect "fidl/fuchsia/inspect/deprecated"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/google/netstack/tcpip"
)

type testStatistic struct {
	value uint64
}

func (s *testStatistic) Value() uint64 {
	return s.value
}

type TestAggregate struct {
	S1      *testStatistic
	S2      *tcpip.StatCounter
	ignored struct{}
}

func TestInspectImplStatCounterTraversal(t *testing.T) {
	testStat := testStatistic{value: 42}
	var statCounter tcpip.StatCounter
	statCounter.IncrementBy(45)
	testAggregate := TestAggregate{S1: &testStat, S2: &statCounter}
	v := statCounterInspectImpl{name: "testStatistic", Value: reflect.ValueOf(testAggregate)}

	var s1 inspect.MetricValue
	s1.SetUintValue(testStat.value)
	var s2 inspect.MetricValue
	s2.SetUintValue(statCounter.Value())

	data, err := v.ReadData()
	if err != nil {
		t.Fatalf("v.ReadData() failed: %s", err)
	}

	if diff := cmp.Diff(data.Metrics, []inspect.Metric{
		{Key: "S1", Value: s1},
		{Key: "S2", Value: s2},
	}, cmpopts.IgnoreUnexported(inspect.Metric{})); diff != "" {
		t.Fatalf("data.Metrics mismatch (-want +got):\n%s", diff)
	}
}
