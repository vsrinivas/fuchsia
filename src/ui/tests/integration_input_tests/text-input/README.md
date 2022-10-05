## Debugging Chromium tests using remote devtools

TODO(fxbug.dev/109739): Automate the Chromium remote devtools setup.

A big drawback of Fuchsia's hermetic integration tests, at the time of this
writing, is that all graphical output is sent to a fake hermetic display.

At the moment, this means that you don't get to *see* what is going on, on the
screen of the web engine instance that your test starts up and sends commands
to. This makes debugging sessions, when something goes wrong, exceptionally
frustrating.

Do not fret! There is a fix for that.  While we wait on a future where graphical
output for integration tests becomes a feature flag, you can follow the
instructions below to run your test and connect to your Fuchsia device web
engine instance under test using the chromium remote devtools.  The remote
devtools allow you to inspect the state of the DOM, but also offers you to
*see* what the graphical output looks like on your Fuchsia device, all from
a browser on your host machine.

First, ensure that your host and your device are connected to a network with
a common router. For example, an ethernet network would be such a thing, or
a WiFi network with DHCP enabled. A peer-to-peer network will not
do.

Note the IP address of your Fuchsia device. Let's assume that the address
is `192.168.86.23` for the sake of this example.

Ensure that the web engine is started up to support remote debugging.
If you look at [text-input-chromium/text-input-chromium.cc], the following
pieces of configuration take care of that:

```
fuchsia::web::CreateContextParams params;
params.set_remote_debugging_port(12342);
params.set_features(fuchsia::web::ContextFeatureFlags::VULKAN |
                    fuchsia::web::ContextFeatureFlags::NETWORK |
                    fuchsia::web::ContextFeatureFlags::KEYBOARD);
```

The above selects a port for connecting to your web engine. You need the
`NETWORK` feature to activate the web engine feature to open a Posix port.

Next, we create a frame passing debugging parameters like this:

```
fuchsia::web::CreateFrameParams frame_params;
frame_params.set_debug_name("text-input-chromium");
frame_params.set_enable_remote_debugging(true);
web_context_->CreateFrameWithParams(std::move(frame_params), web_frame_.NewRequest());
```

(you don't need to do any of the above, `text-input-chromium.cc` is already
configured correctly; above text is given for completeness.)

Next, you must add the following into [meta/text-input-test.cml]:

```
{
    include: {
       //"sys/testing/system-test.shard.cml",
    },
    facets: {
        "fuchsia.test": { type: "chromium" },
    },
}
```

Note that you need to uncomment `system-text.shard.cml`, because the `facets`
stanza is in conflict with it.

Uncomment the section below in [text-input-test.cc]:

```
// config.passthrough_capabilities = {
//{Protocol{fuchsia::posix::socket::Provider::Name_},

// Protocol{fuchsia::netstack::Netstack::Name_},
// Protocol{fuchsia::net::interfaces::State::Name_}},
//};
```

This allows the test to use the real netstack, instead of the fake one, which
will allow your host to connect to the chromium remote devtools.

Finally, retarget the netstack routes to point to the protocols handed down
to the test realm by the system.

```
{.capabilities = {Protocol{fuchsia::posix::socket::Provider::Name_},
                  Protocol{fuchsia::netstack::Netstack::Name_},
                  Protocol{fuchsia::net::interfaces::State::Name_}},
 //.source = ChildRef{kNetstack},
 // Use the .source below instead of above,
 // if you want to use Chrome remote debugging.
 .source = ParentRef(),
 .targets = {target}},
```

Once done, you can run the Chromium tests like so:

```
fx test -o --min-severity-logs DEBUG \
  //src/ui/tests/integration_input_tests/text-input \
  --test-filter="*Chromium*"
```

When the test is running, you can navigate to the URL `chrome://inspect#devices`
on your host.  Click "Configure" and enter a new hostport: `192.168.86.23:12342`,
based on (1) the IP address we noted at the very beginning of this text, and the
port we configured in one of the steps above.

Once done (and possibly reloading the page), while the web engine is running
in the test, you will get a menu of links to select from. Clicking `inspect`
will open the inspect window from Chromium devtools on your host, but the
state and the contents will be those of the web engine running in the test.

In case your test terminates too quickly, you may consider adding a lengthy
pause into your test rig, so that you have enough time to establish a connection
with the web engine and poke around.
