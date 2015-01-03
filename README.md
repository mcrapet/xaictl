# SteelSeries XAI Mouse configuration tool

Command line tool. It is written in C using libusb-1.0 (stable API).

## Disclaimer

This piece of code has been written in few hours, years ago. It was written for testing purpose only.
Vendor specific protocol has been reverse engineered with the help of DebugFS and USBMon and.. VirtualBox! It was tested with firmware 1.4.2.

I'm no USB expert, but USB timings for data transfers are wrong and *not reliable at all*!
Sometimes you must execute the command 6-10 times for real write.

## Access to hardware

### Setup udev for user permission

User must belong to `plugdev` group.

```shell
# cat /etc/udev/rules.d/90-xai.rules
SUBSYSTEM=="usb", ATTRS{idVendor}=="1038", ATTRS{idProduct}=="1360", \
  MODE="0666", GROUP="plugdev", \
  RUN="/bin/sh -c 'echo -n $id:1.2 >$sys/bus/usb/drivers/usbhid/unbind'"
```

Note: Not sure that RUN statement is required anymore for actual kernel/udev.
USB interface 2 of mouse used to be taken by kernel usbhid driver and had to be unbinded for user access.

Reload udev and apply new rule:
```shell
# udevadm control --reload-rules && udevadm trigger
```

## Examples

Last command line argument is always profile number (a digit between 1 and 5).

Without any (dash) options, program prints stored profile configuration:

```shell
$ xaictl 2
Profile 2 (current)
---------
CPI1 (led off)  : 600
CPI2 (led on)   : 800
ExactRate (Hz)  : 300
ExactAccel (%)  : 0
ExactAim  (unit): 0 (0x64)
Free mode (unit): 0 (0x64)
LCD brightness  : 5
LCD contrast    : 9

Button 1 : Left Click
Button 2 : Middle Click
Button 3 : Right Click
Button 4 : IE Backward
Button 5 : IE Forward
Button 6 : Disable
Button 7 : Disable
Button 8 : Mouse Wheel Up
Button 9 : Mouse Wheel Down
```

Change profile name, rate and acceleration:

```shell
$ xaictl -a 0 -r 130 -n "Dad's Profile" 1
```

## Software limitations

* No macro entry
* Handle left/right hand mode flag

## Ideas

* Modify several profiles at once.
