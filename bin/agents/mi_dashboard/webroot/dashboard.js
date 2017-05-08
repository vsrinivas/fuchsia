var _webSocket = null;

$(function() {
  _webSocket = new WebSocket('ws://' + window.location.host + '/ws');

  _webSocket.onmessage = function(evt) {
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
})

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
