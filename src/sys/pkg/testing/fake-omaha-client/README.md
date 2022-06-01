# Fake Omaha client

This test-only binary wraps the real lib/omaha-client Omaha client library,
CUPv2 implementation, and state machine. It is designed for protocol
compatibility testing against real or test-only Omaha servers.

Example usage:

```
# 1. Generate an ECDSA key pair:
$ openssl ecparam -genkey -name prime256v1 -noout -out private.pem
$ openssl ec -in private.pem -pubout -out public.pem

# 2. Start a dummy server
$ python -m http.server 12345

# 2. Build and run the omaha client
$ ./fake-omaha-client --key-id 1 \
  --key "$(cat public.pem)" \
  --server "http://127.0.0.1:12345" \
  --app-id "some_app_id" \
  --channel "some_channel"
```