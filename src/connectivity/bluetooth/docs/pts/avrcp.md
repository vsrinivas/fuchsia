# AVRCP PTS Instructions

## Setup
Source code setup:
* Most tests require a build that disables all external profile clients (sometimes called an "arrested" build)
* Other tests, as noted, require a "full build" that includes a user-controllable media player.

Target command-line tools used to pass AVRCP tests in PTS, invoked from the shell when specified in the instructions:
* `bt-avdtp-tool`
* `bt-avrcp-controller`
* `bt-cli`

Target components, invoked from the host when specified in the instructions:
* `bt-avrcp-target.cmx`
* `example_player.cmx`

## IXIT Non-Default Values
* `TSPX_player_feature_bitmask` = 000000000000B701EF02000000000000 *(note this value will change in releases that include https://fxrev.dev/688451)*

## Default test instructions

### Target (TG) tests
1. (target shell 1) `bt-cli`
2. (target shell 2) `bt-avdtp-tool -d 500`
3. (host shell 1) `fx run fuchsia-pkg://fuchsia.com/bt-avrcp-target#meta/bt-avrcp-target.cmx`
4. (host shell 2) `fx run fuchsia-pkg://fuchsia.com/example_player#meta/example_player.cmx`
5. *Start test*

### Control (CT) tests
1. (target shell 1) `bt-avdtp-tool -d 0`
2. (target shell 2) `bt-cli`
3. (target shell 3) `bt-avrcp-controller <peer-id>`
4. *Start test*

## TESTS

### AVRCP/TG/CEC/BV-01-I
1. (target shell 1) `bt-cli`
2. (target shell 2) `bt-avdtp-tool -d 500`
3. (host shell) `fx run fuchsia-pkg://fuchsia.com/bt-avrcp-target#meta/bt-avrcp-target.cmx`
4. *Start test*

### AVRCP/TG/CEC/BV-02-I
1-5. Use default TG instructions
6. (bt-cli) `connect <peer-id>`

### AVRCP/TG/CRC/BV-01-I
* Use default TG instructions

### AVRCP/TG/CRC/BV-02-I
1-5. Use default TG instructions
6. PTS: *"Take action to disconnect all A2DP and/or AVRCP connections"*
7. (bt-cli) `disconnect <peer-id>`

### AVRCP/TG/ICC/BV-01-I
1-5. Use default TG instructions
6. PTS: *"Take action to disconnect all A2DP and/or AVRCP connections"*
7. (bt-cli) `disconnect <peer-id>`

### AVRCP/TG/ICC/BV-02-I
1-5. Use default TG instructions
6. PTS: *"Take action to disconnect all A2DP and/or AVRCP connections"*
7. (bt-cli) `disconnect <peer-id>`

### AVRCP/TG/PTT/BV-01-I
0. *This test requires a full build with user-controllable media player.*
1. (target UI) Start media
2. (target UI) Pause media
3. *Start test*
4. (PTS) *Confirm that a Play operation starts media*
5. (PTS) *Confirm that a Pause operation pauses media*
6. (PTS) *Confirm that a Play operation starts media*
7. PTS: *"Press OK when the IUT is in a state where media is playing."*
8. (PTS) OK
9. *After PTS stops media, restart media before confirming*
10. (PTS) *Confirm that Stop operation stopped media*
11. (PTS) *Confirm that Forward operation takes you to the next track*
12. (PTS) *Confirm that Backward operation takes you to the previous track*

### AVRCP/TG/PTT/BV-02-I
1-5. Use default TG instructions
6. (PTS) *Confirm volume-up and volume-down commands in avrcp-tg component logs*

### AVRCP/TG/MPS/BV-01-I
1. (target shell 1) `bt-cli`
2. (target shell 2) `bt-avdtp-tool -d 500`
3. (host shell 1) `fx run fuchsia-pkg://fuchsia.com/bt-avrcp-target#meta/bt-avrcp-target.cmx`
4. *Start test*
5. *Verify list of media players available on the IUT*

### AVRCP/TG/MPS/BV-02-I
1-5. Use default TG instructions
6. (PTS) *Confirm volume-up and volume-down commands in avrcp-tg component logs*

### AVRCP/TG/MPS/BV-03-I
1-5. Use default TG instructions
6. (PTS) *Confirm volume-up and volume-down commands in avrcp-tg component logs*

### AVRCP/TG/VLH/BV-01-I
0. *This test requires a full build with user-controllable volume.*
1. *Start test*
2. PTS: *"Take action to trigger a [Register Notification, Changed] response for <Volume Changed> to the PTS from the IUT. This can be accomplished by changing the volume on the IUT."*
3. (target device) *Volume Up*

### AVRCP/TG/VLH/BV-02-I
0. *This test requires a full build with user-visible volume levels.*
1. *Start test*
2. PTS: *"Please check the current absolute volume on the IUT."*
3. (target device) *Note current volume level*
4. (PTS) OK
5. PTS: *"If absolute volume has changed press OK."*
6. (PTS) *Validate that volume level is different from previous setting*

### AVRCP/CT/CEC/BV-01-I
1. (target shell 1) `bt-avdtp-tool -d 500`
2. (target shell 2) `bt-cli`
3. (target shell 3) `bt-avrcp-controller <peer-id>`
4. (host shell) `fx run fuchsia-pkg://fuchsia.com/example_player#meta/example_player.cmx`
5. *Start test*
6. PTS: *"Create an AVDTP signaling channel."*
7. (bt-cli) `connect <peer-id>`

### AVRCP/CT/CEC/BV-02-I
1. (target shell 1) `bt-avdtp-tool -d 500`
2. (target shell 2) `bt-cli`
3. (target shell 3) `bt-avrcp-controller <peer-id>`
4. (host shell) `fx run fuchsia-pkg://fuchsia.com/example_player#meta/example_player.cmx`
5. *Start test*

### AVRCP/CT/CRC/BV-01-I
1. (target shell 1) `bt-avdtp-tool -d 500`
2. (target shell 2) `bt-cli`
3. (target shell 3) `bt-avrcp-controller <peer-id>`
4. (host shell) `fx run fuchsia-pkg://fuchsia.com/example_player#meta/example_player.cmx`
5. *Start test*
6. PTS: *"Take action to disconnect all A2DP and/or AVRCP connections."*
7. (bt-cli) `disconnect <peer-id>`

### AVRCP/CT/CRC/BV-02-I
* Use default CT instructions

### AVRCP/CT/PTT/BV-01-I
*Set* `TSPX_establish_avdtp_stream` *IXIT value to* `FALSE` *for this test*
1-4. Use default CT instructions
5. (bt-avrcp-controller) *Send key commands as requested*

### AVRCP/CT/PTT/BV-02-I
1-4. Use default CT instructions
5. (bt-avrcp-controller) *Send key commands as requested*

### AVRCP/CT/CFG/BV-01-C
* Use default CT instructions

### AVRCP/TG/CFG/BV-02-C
* Use default TG instructions

### AVRCP/TG/CFG/BI-01-C
* Use default TG instructions

### AVRCP/CT/PAS/BV-01-C
1-4. Use default CT instructions
5. PTS: *"Take action to send a [List Player Application Setting Attributes] command to the PTS from the IUT."*
6. (bt-avrcp-controller) `get-player-application-settings`

### AVRCP/TG/PAS/BV-02-C
* Use default TG instructions

### AVRCP/CT/PAS/BV-03-C
1-4. Use default CT instructions
5. PTS: *"Take action to send a [List Player Application Setting Attribute Text] command to the PTS from the IUT."*
6. (bt-avrcp-controller) `get-player-application-settings`

### AVRCP/TG/PAS/BV-04-C
* Use default TG instructions

### AVRCP/TG/PAS/BI-01-C
* Use default TG instructions

### AVRCP/CT/PAS/BV-05-C
1-4. Use default CT instructions
5. PTS: *"Take action to send a [List Player Application Setting Value Text] command to the PTS from the IUT."*
6. (bt-avrcp-controller) `get-player-application-settings`

### AVRCP/TG/PAS/BV-06-C
* Use default TG instructions

### AVRCP/TG/PAS/BI-02-C
* Use default TG instructions

### AVRCP/CT/PAS/BV-07-C
1-4. Use default CT instructions
5. PTS: *"Take action to send a [List Player Application Setting Value Text] command to the PTS from the IUT."*
6. (bt-avrcp-controller) `get-player-application-settings`

### AVRCP/TG/PAS/BV-08-C
* Use default TG instructions

### AVRCP/TG/PAS/BI-03-C
* Use default TG instructions

### AVRCP/CT/PAS/BV-09-C
1-4. Use default CT instructions
5. PTS: *"Take action to send a [Get Current Player Application Setting Value] command to the PTS from the IUT."*
6. (bt-avrcp-controller) `get-player-application-settings`

### AVRCP/TG/PAS/BV-10-C
* Use default TG instructions

### AVRCP/TG/PAS/BI-04-C
* Use default TG instructions

### AVRCP/CT/PAS/BV-11-C
1-4. Use default CT instructions
5. PTS: *"Take action to send a [Set Player Application Setting Value] command to the PTS from the IUT."*
6. (bt-avrcp-controller) `set-player-application-settings`

### AVRCP/TG/PAS/BI-05-C
* Use default TG instructions

### AVRCP/CT/MDI/BV-01-C
1-4. Use default CT instructions
5. PTS: *"Take action to send a [Get Play Status] command to the PTS from the IUT."*
6. (bt-avrcp-controller) `get-play-status`

### AVRCP/TG/MDI/BV-02-C
* Use default TG instructions

### AVRCP/TG/MDI/BV-04-C
0. *This test requires a full build.*
1. *Start test*

### AVRCP/TG/MDI/BV-05-C
0. *This test requires a full build.*
1. *Start test*

### AVRCP/CT/NFY/BV-01-C
* Use default CT instructions

### AVRCP/TG/NFY/BV-02-C
0. *This test requires a full build with user-controllable media player.*
1. *Start test*
2. PTS: *"Take action to trigger a [Register Notification, Changed] response for <Track Changed> to the PTS form the IUT. This can be accomplished by changing the currently playing track on the IUT."*
3. (target device) *Play music and change track when requested*

### AVRCP/TG/NFY/BV-04-C
1-5. Use default TG instructions
6. PTS: *"Place the IUT into a state where no track is currently selected, then press 'OK' to continue."*
7. (PTS) OK

### AVRCP/TG/NFY/BV-05-C
0. *This test requires a full build with user-controllable media player.*
1. (target device) *Start music playing*
2. *Start test*

### AVRCP/TG/NFY/BV-08-C
0. *This test requires a full build with user-controllable media player.*
1. (target device) *Start music playing*
2. (target device) *Pause music*
3. *Start test*

### AVRCP/TG/NFY/BI-01-C
* Use default TG instructions

### AVRCP/TG/INV/BI-01-C
* Use default TG instructions

### AVRCP/TG/INV/BI-02-C
* Use default TG instructions

### AVRCP/TG/MPS/BV-02-C
* Use default TG instructions

### AVRCP/TG/MPS/BI-01-C
* Use default TG instructions

### AVRCP/TG/MPS/BV-06-C
* Use default TG instructions

### AVRCP/TG/MPS/BV-09-C
* Use default TG instructions

### AVRCP/TG/MPS/BV-12-C
* Use default TG instructions

### AVRCP/CT/VLH/BV-01-C
1-4. Use default CT instructions
5. PTS: *"Take action to send a [Set Absolute Volume] command to the PTS from the IUT."*
6. (bt-avrcp-controller) `set-volume 10`

### AVRCP/CT/VLH/BI-03-C
1-4. Use default CT instructions
5. PTS: *"Take action to send a [Set Absolute Volume] command to the PTS from the IUT."*
6. (bt-avrcp-controller) `set-volume 10`
7. PTS: *"PTS has indicated that the current absolute volume is 7%, did the IUT update the volume level correctly?"*
8. (PTS) Yes

### AVRCP/CT/VLH/BI-04-C
1-4. Use default CT instructions
5. PTS: *"PTS has indicated that the current absolute volume is 50%, did the IUT update the volume level correctly?"*
6. (bt-avrcp-controller) *Verify that console shows* `Volume event: <peer-id> 64`

### AVRCP/TG/VLH/BV-02-C
* Use default TG instructions

### AVRCP/TG/VLH/BI-01-C
* Use default TG instructions

### AVRCP/TG/VLH/BI-02-C
* Use default TG instructions

### AVRCP/CT/VLH/BV-03-C
* Use default CT instructions

### AVRCP/TG/VLH/BV-04-C
0. *This test requires a full build*
1. *Start test*

### AVRCP/CT/VLH/BV-01-I
1-4. Use default CT instructions
5. PTS: *"PTS has indicated that the current absolute volume is 50%, did the IUT update the volume level correctly?"*
6. (bt-avrcp-controller) *Verify that console shows* `Volume event: <peer-id> 64`

### AVRCP/CT/VLH/BV-02-I
1-4. Use default CT instructions
5. PTS: *"Take action to send a [Set Absolute Volume] command to the PTS from the IUT."*
6. (bt-avrcp-controller) `set-volume 10`

### AVRCP/CT/RCR/BV-01-C
1-4. Use default CT instructions
5. PTS: *"Take action to send a [Get Element Attributes] command requesting a Title Attribute to the PTS from the IUT."*
6. (bt-avrcp-controller) `get-media`

### AVRCP/TG/RCR/BV-02-C
0. *This test requires a full build*
1. *Modify the metadata of a track on a phone or other device that has a media player that can stream to the target device.*
2. *Start playing the track on the target device.*
3. *Start test*

### AVRCP/TG/RCR/BV-04-C
0. *This test requires a full build*
1. *Modify the metadata of a track on a phone or other device that has a media player that can stream to the target device.*
2. *Start playing the track on the target device.*
3. *Start test*

### AVRCP/CT/PTH/BV-01-C
*Set* `TSPX_establish_avdtp_stream` *IXIT value to* `FALSE` *for this test*
1-4. Use default CT instructions
5. (bt-avrcp-controller) *Send key commands as requested*
