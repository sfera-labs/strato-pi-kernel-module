# Strato Pi kernel module

Raspberry Pi kernel module for using [Strato Pi](https://www.sferalabs.cc/strato/) via sysfs.

For instance, check if the watchdog is enabled:

    cat /sys/class/stratopi/watchdog/enabled
    
Enable the watchdog:

    echo 1 > /sys/class/stratopi/watchdog/enabled
    
Requires Strato Pi CM with firmware version >= 3.5 or any other Strato Pi with firmware version >= 4.0.

## Compile and Install

If you don't have git installed:

    sudo apt install git

Clone this repo:

    git clone --depth 1 --recursive https://github.com/sfera-labs/strato-pi-kernel-module.git
    
Install the Raspberry Pi kernel headers:

    sudo apt install raspberrypi-kernel-headers

Make and install:

    cd strato-pi-kernel-module
    make
    sudo make install
    
Load the module:

    sudo insmod stratopi.ko

Check that it was loaded correctly from the kernel log:

    sudo tail -f /var/log/kern.log

You will see something like:

    ...
    Sep  3 11:49:18 raspberrypi kernel: [   59.855723] stratopi: - | init
    ...
    Sep  3 11:49:18 raspberrypi kernel: [   60.043024] stratopi: - | ready
    ...

Optionally, to have the module automatically loaded at boot add `stratopi` in `/etc/modules`.

E.g.:

    sudo sh -c "echo 'stratopi' >> /etc/modules"

Optionally, to be able to use the `/sys/` files not as super user, create a new group "stratopi" and set it as the module owner group by adding an udev rule:

    sudo groupadd stratopi
    sudo cp 99-stratopi.rules /etc/udev/rules.d/

and add your user to the group, e.g., for user "pi":

    sudo usermod -a -G stratopi pi

then re-login to apply the group change and reload the module:

    su - $USER
    sudo rmmod stratopi.ko
    sudo insmod stratopi.ko
    
When loading the module, it performs an autodetect of the Strato Pi model. To bypass the autodetect you can load it passing the `model_num` parameter:

|Hardware|`model_num`|
|--------|:---------:|
|Strato Pi Base rev. < 3.0|1|
|Strato Pi UPS rev. < 3.0|2|
|Strato Pi CAN|3|
|Strato Pi CM|4|
|Strato Pi Base rev. ≥ 3.0|5|
|Strato Pi UPS rev. ≥ 3.0|6|
|Strato Pi CM Duo|7|
|Strato Pi CAN rev. ≥ 2.0|8|

For instance, for Strato Pi CM Duo:

    sudo insmod stratopi.ko model_num=7
    
If you have the module automatically loaded at boot, you can pass the `model_num` parameter to the module by creating the file `/etc/modprobe.d/stratopi.conf` which contains the line

    options stratopi model_num=<n>
    
Where `<n>` is set to the appropriate value.

## Usage

After loading the module, you will find all the available devices under the directory `/sys/class/stratopi/`.

The following paragraphs list all the possible devices (directories) and files coresponding to Strato Pi's features. 
Depending on the model you will find the available ones.

You can read and/or write to these files to configure, monitor and control your Strato Pi.

Files written in _italic_ are configuration parameters further detailed in the [Strato Pi Logic Controller Advanced Configuration Guide](https://www.sferalabs.cc/files/strato/doc/stratopi-logic-controller-advanced-configuration-guide.pdf).    
Configuration parameters marked with * are not persistent, i.e. their values are reset to default after a power cycle. To change the default values use the `/mcu/config` file (see below).    
Configuration parameters not marked with * are permanently saved each time they are changed, so that their value is retained across power cycles or MCU resets.    
This allows to have a different configuration during the boot up phase, even after an abrupt shutdown. For instance, you may want a short watchdog timeout while your application is running, but it needs to be reset to a longer timeout when a power cycle occurs so that Strato Pi has the time to boot and restart your application handling the watchdog heartbeat.

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
|enabled|R/W|0|Watchdog enabled|
|enabled|R/W|1|Watchdog disabled|
|enabled|W|F|Flip watchdog enabled state|
|expired|R|0|Watchdog timeout not expired|
|expired|R|1|Watchdog timeout expired|
|heartbeat|W|0|Set watchdog heartbeat line low|
|heartbeat|W|1|Set watchdog heartbeat line high|
|heartbeat|W|F|Flip watchdog heartbeat state|
|_enable_mode_*|R/W|D|MCU config XWED - Watchdog normally disabled (factory default)|
|_enable_mode_*|R/W|A|MCU config XWEA - Watchdog always enabled|
|_timeout_*|R/W|&lt;t&gt;|MCU config XWH&lt;t&gt; - Watchdog heartbeat timeout, in seconds (1 - 99999). Factory default: 60|
|_down_delay_*|R/W|&lt;t&gt;|MCU config XWW&lt;t&gt; - Forced shutdown delay from the moment the timeout is expired and the shutdown cycle has not been enabled, in seconds (1 - 99999). Factory default: 60|
|_sd_switch_|R/W|&lt;n&gt;|MCU config XWSD&lt;n&gt; (0 &lt; n &lt; 9) - Switch boot from SDA/SDB after &lt;n&gt; consecutive watchdog resets, if no heartbeat is detected. A value of n > 1 can be used with /enable_mode set to A only; if /enable_mode is set to D, then /sd_switch is set automatically to 1|
|_sd_switch_|R/W|0|MCU config XWSD0 - SD switch on watchdog reset disabled (factory default)|

### Power - `/sys/class/stratopi/power/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|down_enabled|R/W|0|Delayed shutdown cycle disabled|
|down_enabled|R/W|1|Delayed shutdown cycle enabled|
|_down_delay_*|R/W|&lt;t&gt;|MCU config XPW&lt;t&gt; - Shutdown delay from the moment it is enabled, in seconds (1 - 99999). Factory default: 60|
|_off_time_*|R/W|&lt;t&gt;|MCU config XPO&lt;t&gt; - Duration of power-off, in seconds (1 - 99999). Factory default: 5|
|_up_delay_*|R/W|&lt;t&gt;|MCU config XPU&lt;t&gt; - Power-up delay after main power is restored, in seconds (0 - 99999). Factory default: 0|
|_down_enable_mode_*|R/W|I|MCU config XPEI - Immediate (factory default): when shutdown is enabled, Strato Pi will immediately initiate the power-cycle, i.e. wait for the time specified in /down_delay and then power off the Pi board for the time specified in /off_time|
|_down_enable_mode_*|R/W|A|MCU config XPEA - Arm: enabling shutdown will arm the shutdown procedure, but will not start the power-cycle until the shutdown enable line goes low again (i.e. shutdown disabled or Raspberry Pi switched off). After the line goes low, Strato Pi will initiate the power-cycle|
|_up_mode_*|R/W|A|MCU config XPPA - Always: if shutdown is enabled when the main power is not present, only the Raspberry Pi is turned off, and the power is always restored after the power-off time, even if running on battery, with no main power present|
|_up_mode_*|R/W|M|MCU config XPPM - Main power (factory default): if shutdown is enabled when the main power is not present, the Raspberry Pi and the Strato Pi UPS board are powered down after the shutdown wait time, and powered up again only when the main power is restored|
|_sd_switch_|R/W|1|MCU config XPSD1 - Switch boot from SDA/SDB at every power-cycle|
|_sd_switch_|R/W|0|MCU config XPSD0 - SD switch at power-cycle disabled (factory default)|

### RS-485 Config - `/sys/class/stratopi/rs485/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|_mode_*|R/W|A|MCU config XSMA - Automatic (factory default): TX/RX switching is done automatically, based on speed and number of bits detection|
|_mode_*|R/W|P|MCU config XSMP - Passive: TX/RX switching is not actively controlled by Strato Pi|
|_mode_*|R/W|F|MCU config XSMF - Fixed: TX/RX switching is based on speed, number of bits, parity and number ofstop bits set in /params|
|_params_*|R/W|&lt;rbps&gt;|MCU config XSP&lt;rbps&gt; - Set RS-485 communication parameters: baud rate (r), number of bits (b), parity (p) and number of stop bits (s) for fixed mode, see below tables|

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
|_power_delay_*|R/W|&lt;t&gt;|MCU config XUB&lt;t&gt; - UPS automatic power-cycle timeout, in seconds (0 - 99999). Strato Pi UPS will automatically initiate a delayed power-cycle (just like when /power/down_enabled is set to 1) if the main power source is not available for the number of seconds set. A value of 0 (factory default) disables the automatic power-cycle|

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
|blink|W|&lt;t&gt;|LED on for &lt;t&gt; ms|
|blink|W|&lt;t_on&gt; &lt;t_off&gt; &lt;rep&gt;|LED blink &lt;rep&gt; times with &lt;t_on&gt;/&lt;t_off&gt; ms periods. E.g. "200 50 3"|

### Button - `/sys/class/stratopi/button/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|status|R|0|Button released|
|status|R|1|Button pressed|

### Expansion Bus - `/sys/class/stratopi/expbus/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|enabled|R/W|0|Expansion Bus enabled|
|enabled|R/W|1|Expansion Bus disabled|
|aux|R|0|Expansion Bus auxiliary line low|
|aux|R|1|Expansion Bus auxiliary line high|

### SD - `/sys/class/stratopi/sd/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|_sdx_enabled_*|R/W|1|MCU config XSD01 - SDX bus enabled (factory default)|
|_sdx_enabled_*|R/W|0|MCU config XSD00 - SDX bus disabled|
|sdx_enabled|R/W|2|MCU config XSD02 - SDX bus disabled, reset to enabled upon power cycle (FW ver. >= 4.4)|
|_sd1_enabled_*|R/W|1|MCU config XSD11 - SD1 bus enabled|
|_sd1_enabled_*|R/W|0|MCU config XSD10 - SD1 bus disabled (factory default)|
|sd1_enabled|R/W|2|MCU config XSD12 - SD1 bus enabled, reset to disabled upon power cycle (FW ver. >= 4.4)|
|_sdx_default_|R/W|A|MCU config XSDPA - At power-up, SDX bus routed to SDA and SD1 bus to SDB by default (factory default)|
|_sdx_default_|R/W|B|MCU config XSDPB - At power-up, SDX bus routed to SDB and SD1 bus to SDA, by default|
|_sdx_routing_|R/W|A|MCU config XSDRA - SDX bus routed to SDA and SD1 bus to SDB (factory default)|
|_sdx_routing_|R/W|B|MCU config XSDRB - SDX bus routed to SDB and SD1 bus to SDA|

### USB 1 - `/sys/class/stratopi/usb1/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|disabled|R/W|0|USB 1 enabled|
|disabled|R/W|1|USB 1 disabled|
|ok|R|0|USB 1 fault|
|ok|R|1|USB 1 ok|

### USB 2 - `/sys/class/stratopi/usb2/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|disabled|R/W|0|USB 2 enabled|
|disabled|R/W|1|USB 2 disabled|
|ok|R|0|USB 2 fault|
|ok|R|1|USB 2 ok|

### MCU - `/sys/class/stratopi/mcu/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|config|W|S|MCU command XCCS - Save the current configuration as default to be retained across power cycles|
|config|W|R|MCU command XCCR - Restore the original factory configuration and default values|
|fw_version|R|&lt;m&gt;.&lt;n&gt;/&lt;mc&gt;|MCU command XFW? - Read the firmware version, &lt;m&gt; is the major version number, &lt;n&gt; is the minor version number, &lt;mc&gt; is the model code. E.g. "4.0/07" (for firmware versions < 4.0 the model code is not returned)|
|fw_install|W|<fw_file>|Set the MCU in boot-loader mode and upload the specified firmware HEX file|
|fw_install_progress|R|&lt;p&gt;|Progress of the current firmware upload process as percentage|

#### Firmware upload

The `/sys/class/stratopi/mcu/fw_install` sysfs file allows to upload a new firmware on Strato Pi's MCU. 

To this end, output the content of the firmaware HEX file to `/sys/class/stratopi/mcu/fw_install` and then monitor the progress reading from `/sys/class/stratopi/mcu/fw_install_progress`. 

The MCU will be set to boot-loader mode and the firmware uploaded. When the progress reaches 100% you need to disable boot-loader mode by triggering a power-cycle, which is done by setting the shutdown line low (i.e. set `/sys/class/stratopi/power/down_enabled` to 0 or switch off the Raspberry Pi).

For troubleshooting or monitoring the firmware upload process check the kernel log in `/var/log/kern.log`.

Firmware upload axample:

    $ cat firmware.hex > /sys/class/stratopi/mcu/fw_install &
    [1] 14918
    $ cat /sys/class/stratopi/mcu/fw_install_progress
    0
    [...]
    $ cat /sys/class/stratopi/mcu/fw_install_progress
    100
    $ sudo reboot
