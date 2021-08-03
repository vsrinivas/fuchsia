# Fuchsia syslog structured backend

The Fuchsia syslog structured backend allows you to write structured logs to
Archivist. Structured logging allows you to encode more information in your log
than just text, allowing for richer analytics and diagnostics of your component.
This library is meant for systems integrators, not application developers.
https://fxbug.dev/81491 tracks app development support.

## Usage

Before using the API, you need to connect to Archivist using the LogSink
protocol. After connecting, invoke ConnectStructured on the LogSink and pass in
a socket. Use the other side of the socket to do any of the following:

### Encoding a regular key-value-pair message

```cpp
fuchsia_syslog::LogBuffer buffer;
buffer.BeginRecord(severity, file_name, line, msg, condition, false /* is_printf */, logsink_socket, number_of_dropped_messages, pid, tid);
number_of_dropped_message+=buffer.FlushRecord() ? 0 : 1;
```

### Encoding a C printf message

```cpp
fuchsia_syslog::LogBuffer buffer;
buffer.BeginRecord(severity, file_name, line, "Constant C format string %i %s", condition, true /* is_printf */, logsink_socket, number_of_dropped_messages, pid, tid);
// NOTE: In printf encoding you MUST NOT
// name your keys.
buffer.WriteKeyValue(FUCHSIA_SYSLOG_PRINTF_KEY, 5);
buffer.WriteKeyValue(FUCHSIA_SYSLOG_PRINTF_KEY, "some message");
// Anything that has a name will not be considered part of printf
buffer.WriteKeyValue("some key", "some value");
// Ordering matters -- this is a key-value-pair with no named key
// but is not part of printf.
buffer.WriteKeyValue(FUCHSIA_SYSLOG_PRINTF_KEY, "unnamed value");
// FlushRecord returns false if the socket write fails.
// it returns true on success.
// The caller may choose to retry on failure
// instead of dropping the message.
number_of_dropped_message+=buffer.FlushRecord() ? 0 : 1;
```

NOTE: Logs are best-effort (as are all diagnostics), a return value of true from
FlushRecord means that the log was made available to the platform successfully,
but it is not a guarantee that the message will make it to the readable log (for
many reasons, including budget constraints, platform issues, or perhaps we're
running on a build without logging at all).

### API usage from C

The C API is similar to the C++ API. This is the same printf example but in C

```c
log_buffer_t buffer;
syslog_begin_record(&buffer, severity, file_name, line, "Constant C format string %i %s", condition, true /* is_printf */, logsink_socket, number_of_dropped_messages, pid, tid);
// NOTE: In printf encoding you MUST NOT
// name your keys.
syslog_write_key_value_int64(&buffer, FUCHSIA_SYSLOG_PRINTF_KEY, 0, 5);
syslog_write_key_value_string(&buffer, FUCHSIA_SYSLOG_PRINTF_KEY, 0, "some message", strlen("some message"));
// Anything that has a name will not be considered part of printf
syslog_write_key_value_string(&buffer, "some key", strlen("some key"), "some value", strlen("some value"));
// Ordering matters -- this is a key-value-pair with no named key
// but is not part of printf.
syslog_write_key_value_string(&buffer, FUCHSIA_SYSLOG_PRINTF_KEY, 0 "unnamed value", strlen("unnamed value"));
// FlushRecord returns false if the socket write fails.
// it returns true on success.
number_of_dropped_message+=syslog_flush_record(&buffer) ? 0 : 1;
```