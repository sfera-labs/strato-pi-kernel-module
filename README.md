# Strato Pi driver kernel module

Raspberry Pi kernel module for using [Strato Pi](https://www.sferalabs.cc/strato-pi/) via sysfs.

For instance, check if the watchdog is enabled:

    cat /sys/class/stratopi/watchdog/enabled
    
Enable the watchdog:

    echo 1 > /sys/class/stratopi/watchdog/enabled

## Compile and Install

If you don't have git installed:

    sudo apt-get install git-core

Clone this repo:

    git clone --recursive https://github.com/sfera-labs/strato-pi-kernel-module.git
    
Install the Raspberry Pi kernel headers:

    sudo apt-get install raspberrypi-kernel-headers

Make and install:

    cd strato-pi-kernel-module
    make
    sudo make install
    
Load the module:

    sudo insmod stratopi.ko model=<xxx>
    
where `<xxx>` is one between `base`, `ups`, `can`, `cm`, or `cmduo`.

Check that it was loaded correctly from the kernel log:

    sudo tail -f /var/log/kern.log

You will see something like:

    ...
    Aug  9 14:24:12 raspberrypi kernel: [ 6022.987555] Strato Pi: init model=ups
    Aug  9 14:24:12 raspberrypi kernel: [ 6022.989117] Strato Pi: ready
    ...

Optionally, to have the module automatically loaded at boot add `stratopi` in `/etc/modules`, then cretate the file `/etc/modprobe.d/stratopi.conf` and add the line:

    options stratopi model=<xxx>
    
specifying the correct model.

E.g.:

    sudo sh -c "echo 'stratopi' >> /etc/modules"
    sudo sh -c "echo 'options stratopi model=ups' > /etc/modprobe.d/stratopi.conf"

Optionally, to be able to use the `/sys/` files not as super user, create a new group "stratopi" and set it as the module owner group by adding an udev rule:

    sudo groupadd stratopi
    sudo cp 99-stratopi.rules /etc/udev/rules.d/

and add your user to the group, e.g., for user "pi":

    sudo usermod -a -G stratopi pi

then reload the module:

    sudo rmmod stratopi.ko
    sudo insmod stratopi.ko model=<xxx>

## Usage

After loading the module, you will find all the available devices under the directory `/sys/class/stratopi/`.

The following paragraphs list all the possible devices (directories) and files coresponding to Strato's features. 
Depending on the model you will find the available ones.

You can read and/or write to these files to configure, monitor and control your Strato Pi.

The values of the configuration files marked with * can be permanently saved in the Strato Pi controller using the `/mcu/config` file.
If not permanently saved, the parameters will be reset to the original factory defaults, or to
the previously saved user configuration, after every power cycle of the Raspberry Pi.

### Buzzer - `/sys/class/stratopi/buzzer/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|status|R/W|0|Buzzer off|
|status|R/W|1|Buzzer on|
|status|W|F|Flip buzzer's state|
|beep|W|&lt;t&gt;|Buzzer on for &lt;t&gt; ms|
|beep|W|&lt;t_on&gt; &lt;t_off&gt; &lt;rep&gt;|Buzzer beep &lt;rep&gt; times with &lt;t_on&gt;/&lt;t_off&gt; ms periods. E.g. "200 50 3"|

Examples:

    cat /sys/class/stratopi/buzzer/status
    echo F > /sys/class/stratopi/buzzer/status
    echo 200 50 3 > /sys/class/stratopi/buzzer/beep

### Watchdog - `/sys/class/stratopi/watchdog/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|enabled|R/W|0|Watchdog on|
|enabled|R/W|1|Watchdog off|
|enabled|W|F|Flip watchdog enabled state|
|expired|R|0|Watchdog timeout not expired|
|expired|R|1|Watchdog timeout expired|
|heartbeat|W|0|Set watchdog heartbeat pin low|
|heartbeat|W|0|Set watchdog heartbeat pin high|
|heartbeat|W|F|Flip watchdog heartbeat state|
|enable_mode*|R/W|D|Watchdog normally disabled (factory default)|
|enable_mode*|R/W|A|Watchdog always enabled|
|timeout*|R/W|&lt;t&gt;|Watchdog heartbeat timeout, in seconds (1 - 99999)|
|sd_switch*|R/W|0|Switch boot from SDA/SDB every time the watchdog resets the Pi. Can be used with /enable_mode set to D or A|
|sd_switch*|R/W|&lt;n&gt;|Switch boot from SDA/SDB after &lt;n&gt; consecutive watchdog resets, if no heartbeat is detected. Can be used with /enable_mode set to A only; if /enable_mode is set to D, then /sd_switch is set automatically to 0|

### Shutdown - `/sys/class/stratopi/shutdown/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|enabled|R/W|0|Shutdown cycle enabled|
|wait*|R/W|&lt;t&gt;|Shutdown wait time, in seconds (1 - 99999)|
|duration*|R/W|&lt;t&gt;|Duration of power-off, in seconds (1 - 99999)|
|delay*|R/W|&lt;t&gt;|Power-up delay after main power is restored, in seconds (1 - 99999)|
|enable_mode*|R/W|I|Immediate (factory default): when shutdown is enabled, Strato Pi will immediately initiate the power off cycle, wait 60 seconds (configurable with /wait) and then power off the Pi board for at least 5 seconds (configurable with /duration)|
|enable_mode*|R/W|A|Arm: enabling shutdown will arm the shutdown procedure, but will not start the power off cycle until the shutdown enable line goes low again (i.e. shutwown disabled or Raspberry Pi switched off). After the line goes low, Strato Pi will wait 60 seconds (configurable with /wait) and then power off the Pi board for at least 5 seconds (configurable with /duration)|
|up_mode*|R/W|A|Always: if shutdown is enabled when the main power is not present, only the Raspberry Pi is turned off, and the power is always restored after the power-off time, even if running on battery, with no main power present|
|up_mode*|R/W|M|Main power (factory default): if shutdown is enabled when the main power is not present, the Raspberry Pi and the Strato UPS board are powered down after the shutdown wait time, and turned on again only when the main power is restored|

### RS-485 Config - `/sys/class/stratopi/rs485/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|mode*|R/W|A|Automatic (factory default): TX/RX switching is done automatically, based on speed and number of bits detection|
|mode*|R/W|P|Passive: TX/RX switching is not actively controlled by Strato Pi|
|mode*|R/W|F|Fixed: TX/RX switching is based on speed, number of bits, parity and number ofstop bits set in /params|
|params*|R/W|&lt;rbps&gt;|Set RS-485 communication parameters: baud rate (r), number of bits (b), parity (p) and number of stop bits (s) for fixed mode, see below tables|

|Baud rate (r) value|Description|
|---------------|-----------|
|2|1200 bps|
|3|2400 bps|
|4|4800 bps|
|5|9600 bps (factory default)|
|6|19200 bps|
|7|38400 bps|
|8|57600 bps|
|9|115200 bps|

|Bits (b) value|Description|
|---------------|-----------|
|7|7 bit|
|8|8 bit (factory default)|


|Parity (p) value|Description|
|---------------|-----------|
|N|No parity (factory default)|
|E|Even parity|
|O|Odd parity|


|Stop bits (s) value|Description|
|---------------|-----------|
|1|1 stop bit (factory default)|
|2|2 stop bits|

### UPS - `/sys/class/stratopi/ups/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|battery|R|0|Running on main power|
|battery|R|1|Running on battery power|
|power_off*|R/W|&lt;t&gt;|UPS automatic power-off timeout, in seconds. Strato Pi UPS will automatically initiate a delayed power-off cycle (just like when /shutdown/enabled is set to 1) if the main power source is not available for the number of seconds set (1 - 99999)|

### Relay - `/sys/class/stratopi/relay/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|status|R/W|0|Relay open|
|status|R/W|1|Relay closed|
|status|W|F|Flip relay's state|

### LED - `/sys/class/stratopi/led/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|status|R/W|0|LED off|
|status|R/W|1|LED on|
|status|W|F|Flip LED's state|
|beep|W|&lt;t&gt;|LED on for &lt;t&gt; ms|
|beep|W|&lt;t_on&gt; &lt;t_off&gt; &lt;rep&gt;|LED blink &lt;rep&gt; times with &lt;t_on&gt;/&lt;t_off&gt; ms periods. E.g. "200 50 3"|

### Button - `/sys/class/stratopi/button/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|status|R|0|Button released|
|status|R|1|Button pressed|

### I2C Expansion - `/sys/class/stratopi/i2cexp/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|enabled|R/W|0|I2C Expansion enabled|
|enabled|R/W|1|I2C Expansion disabled|
|feedback|R|0|I2C Expansion feedback not active|
|feedback|R|1|I2C Expansion feedback active|

### SD - `/sys/class/stratopi/sd/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|sdx_enabled*|R/W|E|SDX bus enabled|
|sdx_enabled*|R/W|D|SDX bus disabled|
|sd1_enabled*|R/W|E|SD1 bus enabled|
|sd1_enabled*|R/W|D|SD1 bus disabled|
|sda_bus*|R/W|X|SDA routed to SDX bus, SDB to SD1|
|sda_bus*|R/W|1|SDA routed to SD1 bus, SDB to SDX|

### MCU - `/sys/class/stratopi/mcu/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|config|W|S|Permanently save the current configuration as the new factory default|
|config|W|R|Permanently restore the original factory settings|
|fw_version|R|&lt;m&gt;.&lt;n&gt;|read the firmware version, <m> is the major version number, <n> is the minor version number|
