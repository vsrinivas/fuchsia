# Gazelle

> A gazelle is one of many antelope species in the genus Gazella... Gazelles are
> known as swift animals. - [Wikipedia][wiki-gazelle]

Gazelle is a lightweight replacement for Ermine.

To run it:

    fx set workstation_eng_paused.$BOARD \
        --with=//src/experiences/session_shells/ermine \
        --with=//src/experiences/session_shells/gazelle
    fx build
    ffx session launch fuchsia-pkg://fuchsia.com/workstation_session_gazelle#meta/workstation_routing.cm

[wiki-gazelle]: https://en.wikipedia.org/wiki/Gazelle
