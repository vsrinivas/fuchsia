# Known contracts

## image search

This contract specifies the intent to search for images matching a textual
query. The expected input datum is a query string.

*Example:*

    {
      "image search": {
        "query": "puppies"
      }
    }

## view

This is analogous to an [Android VIEW intent](https://developer.android.com/reference/android/content/Intent.html#ACTION_VIEW).
The expected input data are a URI and its decomposition (a subset of URI
properties, for example as described in the [Dart SDK](https://api.dartlang.org/stable/1.23.0/dart-core/Uri-class.html#instance-properties).

*Example:*

    {
      "view": {
        "uri": "https://www.youtube.com/watch?v=Mx-AllVZ1VY",
        "scheme": "https",
        "host": "www.youtube.com",
        "path": "/watch",
        "query parameters": {
          "v": "Mx-AllVZ1VY"
        }
      }
    }
