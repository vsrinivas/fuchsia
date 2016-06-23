This folder contains hacky code to work around known issues in order to temporarily get things kindof working

Everything in this folder should have an issue on https://fuchsia.atlassian.net/ tracking its removal

Files           Issue   Title
-----------------------------------------------------------------------------------------------------------
libdl-stubs.c   MA-7    Remove stray instances of libdl calls in Mesa
drm/*           MA-8    Track down remaining libdrm dependencies in mesa and convert to magma dependencies