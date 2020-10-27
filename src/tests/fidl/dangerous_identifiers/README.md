# FIDL Dangerous Identifiers Tests

The script in `generate` takes a list of lower\_camel\_case identifiers
(in [generate/identifiers.py](generate/identifiers.py)) formats them in a
variety of styles (from [generate/styles.py](generate/styles.py)) and then
uses them in a variety of FIDL contexts (defined in 
[generate/uses.py](generate/uses.py)).

This results in FIDL [libraries](fidl) using these identifiers in many
different ways and build rules for Rust and C++ that build them. These are
used to verify that FIDL generators generate valid source even when presented
with problematic identifiers from FIDL.

