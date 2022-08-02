# A2DP PTS Instructions

## Setup
Source code setup:
* configure build to disable all external profile clients (sometimes called an "arrested" build)

Tools used to pass A2DP tests in PTS:
* `bt-avdtp-tool` (invoke before starting tests)
    * For SRC tests, invoke with the argument `-d 500`
    * For SNK tests, invoke with the argument `-d 0`
* `bt-cli` (invoke from device shell when specified in instructions)

## IXIT Values
* Use all defaults

## TESTS

### A2DP/SRC/AS/BV-01-I
1. PTS: `Is the test system properly playing back the media being sent by the IUT?`
2. (PTS) Yes
    * *Note that PTS doesn't play audio, BQC indicated that this was expected*

### A2DP/SRC/SDP/BV-01-I
1. PTS: `Tester could not find the optional SDP attribute named 'Supported Features'. Is this correct?`
2. (PTS) Yes
3. PTS: `Tester could not find the optional SDP attribute named 'Service Name'. Is this correct?`
4. (PTS) Yes
5. PTS: `Tester could not find the optional SDP attribute named 'Provider Name'. Is this correct?`
6. (PTS) Yes
    * *Note that this behavior may change with the implementation of https://fxbug.dev/103814*

### A2DP/SNK/SDP/BV-02-I
1. PTS: `Tester could not find the optional SDP attribute named 'Supported Features'. Is this correct?`
2. (PTS) Yes
3. PTS: `Tester could not find the optional SDP attribute named 'Service Name'. Is this correct?`
4. (PTS) Yes
5. PTS: `Tester could not find the optional SDP attribute named 'Provider Name'. Is this correct?`
6. (PTS) Yes
    * *Note that this behavior may change with the implementation of https://fxbug.dev/103814*

### A2DP/SNK/CC/BV-01-I
1. PTS: `Is the IUT receiving streaming media from PTS?`
2. *Verify that media is being heard from DUT*

### A2DP/SNK/CC/BV-02-I
1. PTS: `Is the IUT receiving streaming media from PTS?`
2. *Verify that media is being heard from DUT*

### A2DP/SNK/CC/BV-05-I
1. PTS: `Is the IUT receiving streaming media from PTS?`
2. *Verify that media is being heard from DUT*

### A2DP/SNK/CC/BV-06-I
1. PTS: `Is the IUT receiving streaming media from PTS?`
2. *Verify that media is being heard from DUT*

### A2DP/SNK/CC/BV-07-I
1. PTS: `Is the IUT receiving streaming media from PTS?`
2. *Verify that media is being heard from DUT*

### A2DP/SNK/CC/BV-08-I
1. PTS: `Is the IUT receiving streaming media from PTS?`
2. *Verify that media is being heard from DUT*

### A2DP/SRC/CC/BV-09-I
*No additional steps necessary*

### A2DP/SRC/CC/BV-10-I
*No additional steps necessary*

### A2DP/SRC/AS/BI-01-I
1. PTS: `Is the test system properly playing back the media being sent by the IUT?`
2. (PTS) Yes
    * *Note that PTS doesn't play audio, BQC indicated that this was expected*

### A2DP/SRC/AS/BI-02-I
*No additional steps necessary*

### A2DP/SNK/SET/BV-01-I
*No additional steps necessary*

### A2DP/SNK/SET/BV-02-I
*No additional steps necessary*

### A2DP/SNK/SET/BV-03-I
*No additional steps necessary*

### A2DP/SNK/SET/BV-05-I
*Follow instructions for taking device out of range and re-connecting three times*

### A2DP/SNK/REL/BV-01-I
*No additional steps necessary*

### A2DP/SNK/REL/BV-02-I
1. *DUT should be playing audio*
2. PTS: `Close the streaming channel.`
3. (bt-avdtp-tool) release-stream \<peer-id>

### A2DP/SNK/SUS/BV-01-I
*No additional steps necessary*

### A2DP/SNK/SUS/BV-02-I
1. *DUT should be playing audio*
2. PTS: `Suspend the streaming channel.`
3. (bt-avdtp-tool) suspend \<peer-id>

### A2DP/SNK/AS/BV-01-I
*No additional steps necessary*

### A2DP/SRC/SET/BV-01-I
*No additional steps necessary*

### A2DP/SRC/SET/BV-02-I
*No additional steps necessary*

### A2DP/SRC/SET/BV-03-I
*No additional steps necessary*

### A2DP/SRC/SET/BV-05-I
1. (Host terminal) `fx shell bt-cli`
2. *Move the IUT out of range when prompted and then move back into range*
3. PTS: `Create an AVDTP signaling channel.`
4. (bt-cli) connect \<peer-id>
5. *Repeat steps 2-4 two more times*

### A2DP/SRC/SET/BV-04-I
*No additional steps necessary*

### A2DP/SRC/SET/BV-06-I
1. *Move the IUT out of range when prompted*
2. PTS: `Press OK when the IUT is ready to allow the PTS to reconnect the AVDTP signaling channel.`
3. *Move the IUT back into range*
4. (PTS) OK
5. *Repeat steps 1-4 two more times*

### A2DP/SRC/REL/BV-01-I
1. PTS: `Close the streaming media channel.`
2. (bt-avdtp-tool) release-stream \<peer-id>

### A2DP/SRC/REL/BV-02-I
*No additional steps necessary*
