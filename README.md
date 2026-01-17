# MIDI to Display

This program has two parts.
The python part reads MIDI signals (in our case, from mainstage) and displays patch names on a TCP server.
The arduino/C++ part allows an ESP32 connected to a SPI-compatible E-Ink display to read data (in our case a patch name) from a TCP server and display it.
Uses port 8001.

## Python part
MIDI signals are read from the macOS builtin "Simple Core MIDI destination". 
When it sees a program change, it reads the patch number from the next byte and checks it against the mapping in the .toml file.
It then outputs the mapped patch name to a TCP Server on port 8001.
If no mapping exists for a patch number, the output is `???` 

### Usage
Place the .py file and configred .toml file in the same directory.
Run the .py file with python.
Configure your virtual MIDI device (such as MainStage) to send program changes to "Simple Core MIDI destination", the builtin macOS MIDI destination.

### Notes
It is recommended not to make your patch names larger than three characters.
Bewarned that this is not a full MIDI parser; program changes are detected by 0xc? bytes. 
If you happen to send such bytes to the midi destination, it could cause problems.

## Arduino Part
Text is received from the TCP server and displayed on the screen.
As soon as connection is lost, text is wiped, ensuring that the text displayed on screen is current and correct.

### Usage
1. Load the .ino file in the Arduino IDE. 
2. Download the required libraries
3. Set the config SSID/Password, IP and port of TCP server (optionally the other values)
4. Upload to the ESP32

### Notes
E-Ink displays do not update very fast, and as such the program should not be set to update much faster than 250 milliseconds at maximum.
Passive refreshing is only useful if you want a live reading of WiFi dBm; otherwise, there is no need to use it.