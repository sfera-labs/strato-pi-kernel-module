#include <linux/device.h>
#include <linux/i2c.h>

ssize_t devAttrAteccSerial_show(struct device* dev,
		struct device_attribute* attr, char *buf);

void ateccAddDriver(void);
