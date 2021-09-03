# Security Static Build Verifiers
This directory contains static build verifiers used during the build to
validate that we are not introducing common security regressions into builds.
These binaries are ran as compiled_actions as part of system assembly see:
//build/security/verifier/ for their usage in the build.

Generally you will not have to modify these verifiers, however if you are
making a large structural change to the system that impacts how they
verify, they may require an update to match the new model.
