# Voila "Hello world" e2e test.

This directory contains an e2e test for Voila.

The test verifies that Voila successfully embeds a replica - that is, that a
session shell ends up being displayed in the embedded view.

This verifies the correctness of view embedding across Voila -> Sessionmgr ->
test shell, allowing us to catch breakages such as LE-746.