// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let html = `
<!DOCTYPE html>
<html>
  <style>
    .button {
      border: none;
      color: white;
      padding: 15px 32px;
      text-align: center;
      text-decoration: none;
      display: inline-block;
      font-size: 16px;
      margin: 4px 2px;
      cursor: pointer;
      border-radius: 10px;
      box-sizing: border-box;
    }

    .button1 {
      background-color: green;
    }
    .button2 {
      background-color: blue;
    }

  </style>
  <body style="text-align: center; background-color: white;">

    <!-- 2 buttons that shows the number of clicks -->
    <button class="button button1" id="btn_green">0</button>
    <button class="button button2" id="btn_blue">0</button>
    <p>Buttons Clicked <span id="display">0</span> Times </p>
    <script type="text/javascript">
      var total_count = 0;
      var btn_green = document.getElementById("btn_green");
      var green_count = 0;
      var btn_blue = document.getElementById("btn_blue");
      var blue_count = 0;
      var disp = document.getElementById("display");
      btn_green.onclick = function() {
        disp.innerHTML = ++total_count;
        btn_green.innerHTML = ++green_count;
      };
      btn_blue.onclick = function() {
        disp.innerHTML = ++total_count;
        btn_blue.innerHTML = ++blue_count;
      };
    </script>

    <!-- Text field that prints what's being typed from keyboard. -->
    <label for="text_input">Type something</label>
    <br />
    <textarea id="text_input" name="Input" rows="3" cols="50"></textarea>
    <p>Typed: " <span id="typed"></span>" </p>
    <script type="text/javascript">
      var text = document.getElementById("text_input");
      var typed = document.getElementById("typed");
      text.oninput = function() {
        typed.innerHTML = text.value;
      };
    </script>
  </body>
</html>
`;

document.write(html);
