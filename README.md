# hd44780-i2c
This is a Linux kernel driver for Hitachi HD44780 LCDs attached to I2C bus via PCF8574 I/O expander. Ideal to use with Raspberry Pi and other small devices running Linux where I2C bus is available.

### Features
The main goal was to expose HD44780-based LCDs behind regular Linux device files normally found in `/dev` directory. Thus, writing to the display is as easy as `echo Hello, world! > /dev/lcd0`.

There are no imposed limitations on number of concurrently attached devices. In practice, a single I2C bus allows up to 128 uniquely addressable devices. Furthermore, standard HD44780 LCD to I2C adapters usually use the same, hardcoded I2C address (like 0x27) and it's not possible to change them. The PCF8574 IC supports up to 8 different addresses, so a custom-build adapter might be the solution.

Multiple LCD geometries are supported (20x4, 16x8 and 8x1) and it's trivial to add new ones if needed.

Supported escape sequences:
* `\r` - carriage return
* `\n` - line feed (new line)

Supported VT100 terminal control escape sequences:
* `<ESC>[H` - cursor home
* `<ESC>[2J` - erase screen

### Usage

TODO - add me
