# firebase

Client library for the REST API of the Firebase Realtime Database.

## curl

In order to manually debug potential sync issues, we can use `curl` to interact
with the database from the command line.

Reading data:

```sh
curl <host>/<path>.json -X GET
```

Writing data:

```sh
curl <host>/<path>.json -X PUT -d <data>
```

Opening a notification stream:

```sh
curl -L <host>/<path>.json -X GET -H "Accept: text/event-stream"
```
