# Gazelle

> A gazelle is one of many antelope species in the genus Gazella... Gazelles are
> known as swift animals. - [Wikipedia][wiki-gazelle]

Gazelle is a lightweight replacement for Ermine.

To run it:

    fx set workstation_eng_paused.BOARD \
        --with //src/experiences/session_shells/{ermine,gazelle} \
        '--args=application_shell="gazelle"'
    fx build
    ffx session launch fuchsia-pkg://fuchsia.com/workstation_routing#meta/workstation_routing.cm

[wiki-gazelle]: https://en.wikipedia.org/wiki/Gazelle
