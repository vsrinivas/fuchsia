const RECONNECT_TIMEOUT = 1000;

var _webSocket = null;
var _reconnectRetries = 0;

$(function() {
  connectWebSocket();
})

function connectWebSocket() {
  _webSocket = new WebSocket('ws://' + window.location.host + '/ws');
  _webSocket.onopen = handleWebSocketOpen;
  _webSocket.onerror = handleWebSocketError;
  _webSocket.onclose = handleWebSocketClose;
  _webSocket.onmessage = handleWebSocketMessage;
}

function handleWebSocketOpen(evt) {
  $('#connectedLabel').text("Connected");
  // reset reconnect retry count
  _reconnectRetries = 0;
}

function handleWebSocketError(evt) {
  console.log("WebSocket Error: " + evt.toString());
}

function handleWebSocketClose(evt) {
  $('#connectedLabel').text("Disconnected");

  // attempt to reconnect
  attemptReconnect();
}

function handleWebSocketMessage(evt) {
  // parse the JSON message
  var message = JSON.parse(evt.data);
  switch (message.type) {
    case "context":
      handleContextMessage(message);
      break;

    // TODO(jwnichols): actionlog, etc.

    default:
      // do nothing
      console.log("[WARN] Websocket message with type " + message.type);
      break;
  }
}

function handleContextMessage(message) {
  for (var topic in message.data) {
    // make a pretty string for the topic's value
    var topicValue;
    var danger;
    try {
      topicValue = JSON.stringify(JSON.parse(message.data[topic]),null,2);
      danger = false; // can't just leave it undefined;
                      // .toggleClass has a 1-arg overload
    } catch (e) {
      topicValue = message.data[topic];
      danger = true;
    }
    var topicId = '#' + $.escapeSelector(topic);
    if ($(topicId).length > 0) {
      // element exists, update the value
      $(topicId).find('pre').text(topicValue)
        .toggleClass('text-danger', danger);
    } else {
      // element does not exist, add it to the table
      $('<tr/>').appendTo("#contextTBody")
        .append($('<td/>').text(topic))
        .append($('<td/>').attr('id', topic)
          .append(($('<pre/>').text(topicValue)
            .toggleClass('text-danger', danger))));
    }
  }
}

function attemptReconnect() {
  // exponential reconnect timeout
  var timeout = Math.pow(2,_reconnectRetries) * RECONNECT_TIMEOUT;
  _reconnectRetries++;
  console.log("Attempting to reconnect after " + timeout);

  // reconnect after the timeout
  setTimeout(connectWebSocket,timeout);
}
