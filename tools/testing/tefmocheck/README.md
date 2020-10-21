# Tefmocheck

Tefmocheck (Testing Failure Mode Checker) analyzes the outputs of a testing
Swarming task and determines whether various failures modes occured. It produces
a testing summary file (summmary.json) that contains all of the tests in the
input summary, as well as a synthetic test for each failure mode starting with
"testing_failure_mode/".

At most a single check (the most specific one possible) will fail on a single
task. The least specific ones start with "testing_failure_mode/task_status/".
This just surfaces the Swarming task status. To see further details, see the
infra_and_test_std_and_klog.txt, which includes the output of the Swarming task.

This tool is invoked by the infrastructure recipes, so any changes to its
interface must be soft transitions.

## Test names

The tests results produced by this tool are analyzed for flakiness. Currently
our flake analysis relies on seeing a test fail and then pass, which means that
the test names must appear in the output summary even if they pass. This means
that we cannot parse a string out of an error message and use that in a test
name. <https://fxbug.dev./62307> tracks improving this.
