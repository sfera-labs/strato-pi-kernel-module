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
    

## Usage

After loading the module, you will find all the available devices under the directory `/sys/class/stratopi/`.

The following paragraphs list all the possible devices (directories) and files coresponding to Strato's features. 
Depending on the model you will find the available ones.

You can read and/or write to these file to use your Strato Pi.

### Buzzer - `/sys/class/stratopi/buzzer/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|status|R/W|0|Buzzer off|
|status|R/W|1|Buzzer on|
|status|W|F|Flip buzzer's state|
|beep|W|&lt;t&gt;|Buzzer on for <t> ms|
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
|enable_mode|R/W|D|Watchdog normally disabled (factory default)|
|enable_mode|R/W|A|Watchdog always enabled|
|timeout|R/W|<t>|Watchdog heartbeat timeout in seconds (1 - 99999)|


... To be continued ...
