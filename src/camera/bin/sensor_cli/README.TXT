
1. To select IMX227 sensor's normal operation mode, use this:

   fx shell camera_sensor_cli --mode=0

1. To select IMX227 sensor's built-in test pattern mode, use this:

   fx shell camera_sensor_cli --mode=3

2. To display Perception ML FR, use this:

   fx camera-gym -m
   fx shell camera-gym-ctl --set-config=0
   fx shell camera-gym-ctl --add-stream=0
