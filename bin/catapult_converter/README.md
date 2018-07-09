
# Catapult performance results converter

This directory contains the `catapult_converter` command line tool
which takes results from performance tests (in Fuchsia's [JSON perf
test result format](../../docs/benchmarking.md#export)) and converts
them to the [Catapult
Dashboard](https://github.com/catapult-project/catapult)'s [JSON
"HistogramSet"
format](https://github.com/catapult-project/catapult/blob/master/docs/histogram-set-json-format.md).

## Parameters

The Catapult dashboard requires the following parameters (called
"diagnostics") to be present in each HistogramSet that is uploaded to
the dashboard:

* chromiumCommitPositions: This parameter is taken from the
  `--execution-timestamp-ms` argument.  The dashboard uses this value
  to order results from different builds in a graph.

* benchmarks: This parameter is taken from the `--test-suite'
  argument.  This is usually the name of the executable containing the
  perf tests, e.g. "zircon_benchmarks".

* masters: The term "master" is an outdated term from when Buildbot
  was used by Chrome infrastructure.  The convention now is to use the
  name of the bucket containing the builder for this parameter.

* bots: The term "bot" is an outdated term from when Buildbot was used
  by Chrome infrastructure.  The convention now is to use the name of
  the builder for this parameter.

For more information on Catapult's format, see [How to Write
Metrics](https://github.com/catapult-project/catapult/blob/master/docs/how-to-write-metrics.md).
