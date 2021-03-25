# Scenic Shadertoy example

This example consists of two parts:
+ a service which:
  + accepts shader code fragments from clients
  + renders images using this shader code
  + presents these images by acting as the client/producer end of a Scenic image pipe
+ a client which connects to both Scenic and the Shadertoy service:
  + provide GLSL code to the service
  + request server/consumer end of an image pipe from the service
  + pass the server/consumer end of the image pipe to Scenic, to embed it at the desired place in the scene graph
