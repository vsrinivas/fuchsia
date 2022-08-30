# Clipboard service

This is a basic clipboard service that implements
[RFC-0179](/docs/contribute/governance/rfcs/0179_basic_clipboard_service.md).

## Testing

```shell
fx set workstation.x64 --with src/ui/bin/clipboard:tests
# ...
fx test //src/ui/bin/clipboard
```