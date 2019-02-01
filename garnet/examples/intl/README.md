# Internationalization Example: Intl Wisdom

Simple example of using `fuchsia.intl.Profile` (from `intl.fidl`) for conveying
internationalization properties to services. Eventually, this might become a
polyglot version of `fortune`.

Run using

```
run fuchsia-pkg://fuchsia.com/intl_wisdom#meta/intl_wisdom_client.cmx
```

You can optionally specify a timestamp in ISO-8601 format, to be used in
generating bits of wisdom:

```
--timestamp=2018-11-01T12:34:56Z
```

You can also override the timezone that will be set in the `Profile`, by using
an
[IANA Time Zone ID](http://en.wikipedia.org/wiki/List_of_tz_database_time_zones#List):

```
--timezone=America/Los_Angeles
```

********************************************************************************

The client command above will automatically start the server. If you'd like to
start the server _directly_, execute

```
run fuchsia-pkg://fuchsia.com/intl_wisdom#meta/intl_wisdom_server.cmx
```
