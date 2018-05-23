# Scenic Benchmarks

We collect benchmarks first by tracing a given process, and then by post-processing
the trace to output benchmark information.

There is only one benchmark here currently but more will be added.

## hello_scenic_benchmark.sh

This script collects a trace of hello_scenic. It takes one parameter, which is
where the outputted benchmarks JSON file should be written.

## process_scenic_trace.go

A Go program that takes the following parameters in this order:
* test label
* trace file name
* output filename for benchmarks
The output is a JSON file with the benchmark output.
