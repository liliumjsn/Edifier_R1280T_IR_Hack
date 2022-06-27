# Edifier R1280T IR Hack

![alt text](https://github.com/liliumjsn/Edifier_R1280T_IR_Hack/blob/main/Photos/IMG_20220609_185720.jpg?raw=true)

<p align="center">
<a href="http://www.youtube.com/watch?feature=player_embedded&v=eyL97ZMiG40
" target="_blank"><img src="https://github.com/liliumjsn/Edifier_R1280T_IR_Hack/blob/main/Photos/62a31fe698654-fbutube-0.jpg" 
alt="IMAGE ALT TEXT HERE" width="240" height="180" border="10" /></a>
 </p>

Modded Edifier R1280T to change volume with Samsung or any other remote. I used an esp8266 which reads the IR command, and communicates with the onboard amplifier (TAS5713) through I2C in order to change the volume. There is a black tinted OLED in front of the speaker that lights up when volume changes. The ESP is also connected to home assistant through mqtt so it can generate other commands from IR remote button presses. Source code available as a platformio project, and 3D printed parts in .3mf format.


***ATTENTION Doing this mod you will lose the knob control of the speakers and obviously your warranty.

## Comments

You will need a separate AC to 5V power supply for the ESP32 because the onboard regulators can't handle it.
Current setup supports AA,BB,CC,DD commands form the tv remote. For example command AD is a specific number,
published in mqtt topic as json:
```
Topic: home/livingroom/tvspeakers/command
{"command":13}
```
