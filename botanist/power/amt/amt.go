// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package amt

import (
	"fmt"
	"io/ioutil"
	"net/http"
	"net/url"
	"strings"

	"fuchsia.googlesource.com/tools/digest"
	"github.com/google/uuid"
)

const (
	// https://software.intel.com/en-us/node/645995
	PowerStateOn             = 2
	PowerStateLightSleep     = 3
	PowerStateDeepSleep      = 4
	PowerStatePowerCycleSoft = 5
	PowerStateOffHard        = 6
	PowerStateHibernate      = 7
	PowerStateOffSoft        = 8
	PowerStatePowerCycleHard = 9
	PowerStateMasterBusReset = 10
)

// Printf string with placeholders for destination uri, message uuid
const payloadTmpl = `
<?xml version="1.0" encoding="UTF-8"?>
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" xmlns:wsa="http://schemas.xmlsoap.org/ws/2004/08/addressing" xmlns:wsman="http://schemas.dmtf.org/wbem/wsman/1/wsman.xsd" xmlns:pms="http://schemas.dmtf.org/wbem/wscim/1/cim-schema/2/CIM_PowerManagementService">
<s:Header>
  <wsa:Action s:mustUnderstand="true">http://schemas.dmtf.org/wbem/wscim/1/cim-schema/2/CIM_PowerManagementService/RequestPowerStateChange</wsa:Action>
  <wsa:To s:mustUnderstand="true">%s</wsa:To>
  <wsman:ResourceURI s:mustUnderstand="true">http://schemas.dmtf.org/wbem/wscim/1/cim-schema/2/CIM_PowerManagementService</wsman:ResourceURI>
  <wsa:MessageID s:mustUnderstand="true">uuid:%s</wsa:MessageID>
  <wsa:ReplyTo><wsa:Address>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</wsa:Address></wsa:ReplyTo>
  <wsman:SelectorSet>
    <wsman:Selector Name="Name">Intel(r) AMT Power Management Service</wsman:Selector>
    <wsman:Selector Name="SystemName">Intel(r) AMT</wsman:Selector>
    <wsman:Selector Name="CreationClassName">CIM_PowerManagementService</wsman:Selector>
    <wsman:Selector Name="SystemCreationClassName">CIM_ComputerSystem</wsman:Selector>
  </wsman:SelectorSet>
</s:Header>
<s:Body>
  <pms:RequestPowerStateChange_INPUT>
    <pms:PowerState>%d</pms:PowerState>
    <pms:ManagedElement>
      <wsa:Address>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</wsa:Address>
      <wsa:ReferenceParameters>
        <wsman:ResourceURI>http://schemas.dmtf.org/wbem/wscim/1/cim-schema/2/CIM_ComputerSystem</wsman:ResourceURI>
        <wsman:SelectorSet>
          <wsman:Selector Name="Name">ManagedSystem</wsman:Selector>
          <wsman:Selector Name="CreationClassName">CIM_ComputerSystem</wsman:Selector>
        </wsman:SelectorSet>
      </wsa:ReferenceParameters>
    </pms:ManagedElement>
  </pms:RequestPowerStateChange_INPUT>
</s:Body>
</s:Envelope>
`

// Reboot sends a Master Bus Reset to an AMT compatible device at host:port.
func Reboot(host, username, password string) error {
	// AMT over http always uses port 16992
	uri, err := url.Parse(fmt.Sprintf("http://%s:16992/wsman", host))
	if err != nil {
		return err
	}
	// Generate MessageID
	uuid := uuid.New()
	payload := fmt.Sprintf(payloadTmpl, uri.String(), uuid, PowerStatePowerCycleSoft)

	t := digest.NewTransport(username, password)
	req, err := http.NewRequest("POST", uri.String(), strings.NewReader(payload))
	if err != nil {
		return err
	}
	res, err := t.RoundTrip(req)
	if err != nil {
		return err
	}
	defer res.Body.Close()

	body, err := ioutil.ReadAll(res.Body)
	if err != nil {
		return err
	}
	returnValue := string(strings.Split(string(body), "ReturnValue>")[1][0])
	if returnValue != "0" {
		return fmt.Errorf("amt reboot ReturnValue=%s", returnValue)
	}

	return nil
}
