# processargs

Processargs is a small static library for parsing messsages in the
[processargs] protocol. The code in this library runs in very limited
contexts. In particular, it may run prior to any sanitizer or
safe-stack runtime being initialized.

[processargs]: ../../../docs/program_loading.md#the-processargs-protocol
