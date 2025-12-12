# esp-webSerial

This project creates a WebSocket server on the ESP32. It reads data from a specific UART port and streams it directly to a web interface hosted on the ESP32. This is useful for debugging remote devices wirelessly.


## Hardware Connection (Pinout)
connect your target device (e.g., a sensor or another MCU) to the ESP32 as follows:

|ESP32 Pin|Function|Connection| 
|-|-|-|
|GPIO 4|UART TX (Transmit)|Connect to Target's RX| 
|GPIO 5|UART RX (Receive)|Connect to Target's TX|
|GND|Ground|Connect to Target's GND|

> Note: The baud rate is set to 115200. Ensure your target device matches this speed.

## Configuration (sdkconfig)
This project uses Kconfig to manage Wi-Fi credentials. You need to configure the SSID and Password before building.

1. Open the configuration men
```
idf.py menuconfig
```
2. Navigate to Wi-Fi Settings
   * Look for a menu  named `WiFi Configuraton`.

3. Set Credentials
   * WiFi SSID: Enter your Wi-Fi Name.
   * WIFI Password: Enter your Wi-Fi Password.

4. Save and Exit
> Tip: If you do not have a Kconfig file set up, you can simply modify the code directly:
```
// Replace these lines in main.c 
#define WIFI_SSID "Your_WiFi_Name"
#define WIFI_PASS "Your_WiFi_Password"
```

## Build and Flash 
Use the ESP-IDF command line tool to build and upload the firmware

1. Set the target (if not done yet) 
```Bash
idf.py set-target esp32
```
2. Build the project 
```Bash
idf.py build
```

3. Flash and Monitor 
   * Replace PORT with your actual serial port (e.g., COM3 on Windows or /dev/ttyUSB0 on Linux/macOS).
```Bash
idf.py -p PORT flash monitor
```

## How to Use 
1. Check IP Address: After flashing, watch the terminal output (monitor). Once connected to Wi-Fi, the ESP32 will print its IP address:
```
I (1234) web_serial: Got IP: 192.168.1.105
I (1244) web_serial: Starting server on port: '80'
```

2. Open Web Interface : Open a web browser and enter the IP address (e.g., http://192.168.1.105).

3. View Data :

   * The web page shows "Connected" in lime color.
   * Any data sent to the ESP32's GPIO 5 (RX) will immediately appear in the browser's black terminal window.