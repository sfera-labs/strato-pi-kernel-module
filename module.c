/*
 * stratopi
 *
 *     Copyright (C) 2019 Sfera Labs S.r.l.
 *
 *     For information, see the Strato Pi web site:
 *     https://www.sferalabs.cc/strato-pi
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * LICENSE.txt file for more details.
 *
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "soft_uart/raspberry_soft_uart.h"

#define MODEL_BASE		101
#define MODEL_UPS		102
#define MODEL_CAN		103
#define MODEL_CM		104
#define MODEL_CMDUO		105

#define SOFT_UART_RX_BUFF_SIZE 	16
#define SOFT_UART_RX_TIMEOUT 	300

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sfera Labs - http://sferalabs.cc");
MODULE_DESCRIPTION("Strato Pi driver module");
MODULE_VERSION("1.0");

static int modelNum;
static char *model = "";
module_param( model, charp, S_IRUGO);
MODULE_PARM_DESC(model, " Strato Pi model - 'base', 'ups', 'can', 'cm', or 'cmduo'");

int GPIO_BUZZER;
int GPIO_WATCHDOG_ENABLE;
int GPIO_WATCHDOG_HEARTBEAT;
int GPIO_WATCHDOG_EXPIRED;
int GPIO_SHUTDOWN;
int GPIO_UPS_BATTERY;
int GPIO_RELAY;
int GPIO_LED;
int GPIO_BUTTON;
int GPIO_I2CEXP_ENABLE;
int GPIO_I2CEXP_FEEDBACK;
int GPIO_SOFTSERIAL_TX;
int GPIO_SOFTSERIAL_RX;

static struct class *pDeviceClass;

static struct device *pBuzzerDevice = NULL;
static struct device *pWatchdogDevice = NULL;
static struct device *pRs485Device = NULL;
static struct device *pShutdownDevice = NULL;
static struct device *pUpsDevice = NULL;
static struct device *pRelayDevice = NULL;
static struct device *pLedDevice = NULL;
static struct device *pButtonDevice = NULL;
static struct device *pI2CExpDevice = NULL;
static struct device *pSdDevice = NULL;
static struct device *pMcuDevice = NULL;

static struct device_attribute devAttrBuzzerStatus;
static struct device_attribute devAttrBuzzerBeep;

static struct device_attribute devAttrWatchdogEnabled;
static struct device_attribute devAttrWatchdogHeartbeat;
static struct device_attribute devAttrWatchdogExpired;
static struct device_attribute devAttrWatchdogEnableMode;
static struct device_attribute devAttrWatchdogTimeout;
static struct device_attribute devAttrWatchdogSdSwitch;

static struct device_attribute devAttrRs485Mode;
static struct device_attribute devAttrRs485Params;

static struct device_attribute devAttrShutdownEnabled;
static struct device_attribute devAttrShutdownWait;
static struct device_attribute devAttrShutdownEnableMode;
static struct device_attribute devAttrShutdownDuration;
static struct device_attribute devAttrShutdownUpDelay;
static struct device_attribute devAttrShutdownUpMode;
static struct device_attribute devAttrShutdownSdSwitch;

static struct device_attribute devAttrUpsBattery;
static struct device_attribute devAttrUpsPowerOff;

static struct device_attribute devAttrRelayStatus;

static struct device_attribute devAttrLedStatus;
static struct device_attribute devAttrLedBlink;

static struct device_attribute devAttrButtonStatus;

static struct device_attribute devAttrI2CExpEnabled;
static struct device_attribute devAttrI2CExpFeedback;

static struct device_attribute devAttrSdSdxEnabled;
static struct device_attribute devAttrSdSd1Enabled;
static struct device_attribute devAttrSdSdxRouting;
static struct device_attribute devAttrSdSdxBoot;

static struct device_attribute devAttrMcuConfig;
static struct device_attribute devAttrMcuFwVersion;

static bool softUartInitialized;
volatile static char softUartRxBuff[SOFT_UART_RX_BUFF_SIZE];
volatile static int softUartRxBuffIdx;

static char toUpper(char c) {
	if (c >= 97 && c <= 122) {
		return c - 32;
	}
	return c;
}

static int getGPIO(struct device* dev, struct device_attribute* attr) {
	if (dev == pBuzzerDevice) {
		return GPIO_BUZZER;
	} else if (dev == pWatchdogDevice) {
		if (attr == &devAttrWatchdogEnabled) {
			return GPIO_WATCHDOG_ENABLE;
		} else if (attr == &devAttrWatchdogHeartbeat) {
			return GPIO_WATCHDOG_HEARTBEAT;
		} else if (attr == &devAttrWatchdogExpired) {
			return GPIO_WATCHDOG_EXPIRED;
		}
	} else if (dev == pShutdownDevice) {
		return GPIO_SHUTDOWN;
	} else if (dev == pUpsDevice) {
		return GPIO_UPS_BATTERY;
	} else if (dev == pRelayDevice) {
		return GPIO_RELAY;
	} else if (dev == pLedDevice) {
		return GPIO_LED;
	} else if (dev == pButtonDevice) {
		return GPIO_BUTTON;
	} else if (dev == pI2CExpDevice) {
		if (attr == &devAttrI2CExpEnabled) {
			return GPIO_I2CEXP_ENABLE;
		} else if (attr == &devAttrI2CExpFeedback) {
			return GPIO_I2CEXP_FEEDBACK;
		}
	}
	return -1;
}

static int getMcuCmd(struct device* dev, struct device_attribute* attr,
		char *cmd) {
	if (dev == pRs485Device) {
		if (attr == &devAttrRs485Mode) {
			cmd[1] = 'S';
			cmd[2] = 'M';
			return 4;
		} else if (attr == &devAttrRs485Params) {
			cmd[1] = 'S';
			cmd[2] = 'P';
			return 7;
		}
	} else if (dev == pShutdownDevice) {
		if (attr == &devAttrShutdownEnableMode) {
			cmd[1] = 'P';
			cmd[2] = 'E';
			return 4;
		} else if (attr == &devAttrShutdownWait) {
			cmd[1] = 'P';
			cmd[2] = 'W';
			return 8;
		} else if (attr == &devAttrShutdownDuration) {
			cmd[1] = 'P';
			cmd[2] = 'O';
			return 8;
		} else if (attr == &devAttrShutdownUpDelay) {
			cmd[1] = 'P';
			cmd[2] = 'U';
			return 8;
		} else if (attr == &devAttrShutdownUpMode) {
			cmd[1] = 'P';
			cmd[2] = 'P';
			return 4;
		} else if (attr == &devAttrShutdownSdSwitch) {
			cmd[1] = 'P';
			cmd[2] = 'S';
			cmd[3] = 'D';
			return 5;
		}
	} else if (dev == pWatchdogDevice) {
		if (attr == &devAttrWatchdogEnableMode) {
			cmd[1] = 'W';
			cmd[2] = 'E';
			return 4;
		} else if (attr == &devAttrWatchdogTimeout) {
			cmd[1] = 'W';
			cmd[2] = 'H';
			return 8;
		} else if (attr == &devAttrWatchdogSdSwitch) {
			cmd[1] = 'W';
			cmd[2] = 'S';
			cmd[3] = 'D';
			return 5;
		}
	} else if (dev == pUpsDevice) {
		if (attr == &devAttrUpsPowerOff) {
			cmd[1] = 'U';
			cmd[2] = 'B';
			return 8;
		}
	} else if (dev == pSdDevice) {
		if (attr == &devAttrSdSdxEnabled) {
			cmd[1] = 'S';
			cmd[2] = 'D';
			cmd[3] = '0';
			return 5;
		} else if (attr == &devAttrSdSd1Enabled) {
			cmd[1] = 'S';
			cmd[2] = 'D';
			cmd[3] = '1';
			return 5;
		} else if (attr == &devAttrSdSdxRouting) {
			cmd[1] = 'S';
			cmd[2] = 'D';
			cmd[3] = 'R';
			return 5;
		} else if (attr == &devAttrSdSdxBoot) {
			cmd[1] = 'S';
			cmd[2] = 'D';
			cmd[3] = 'P';
			return 5;
		}
	} else if (dev == pMcuDevice) {
		if (attr == &devAttrMcuConfig) {
			cmd[1] = 'C';
			cmd[2] = 'C';
			return 4;
		} else if (attr == &devAttrMcuFwVersion) {
			cmd[1] = 'F';
			cmd[2] = 'W';
			return 10;
		}
	}
	return -1;
}

static ssize_t GPIO_show(struct device* dev, struct device_attribute* attr,
		char *buf) {
	int gpio;
	gpio = getGPIO(dev, attr);
	if (gpio < 0) {
		return -EINVAL;
	}
	return sprintf(buf, "%d\n", gpio_get_value(gpio));
}

static ssize_t GPIO_store(struct device* dev, struct device_attribute* attr,
		const char *buf, size_t count) {
	bool val;
	int gpio = getGPIO(dev, attr);
	if (gpio < 0) {
		return -EINVAL;
	}
	if (kstrtobool(buf, &val) < 0) {
		if (toUpper(buf[0]) == 'F' || toUpper(buf[0]) == 'T') {
			val = gpio_get_value(gpio) == 1 ? false : true;
		} else {
			return -EINVAL;
		}
	}
	gpio_set_value(gpio, val ? 1 : 0);
	return count;
}

static ssize_t GPIOBlink_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int i;
	long on = 0;
	long off = 0;
	long rep = 1;
	char *end = NULL;
	int gpio = getGPIO(dev, attr);
	if (gpio < 0) {
		return -EINVAL;
	}
	on = simple_strtol(buf, &end, 10);
	off = simple_strtol(end + 1, &end, 10);
	rep = simple_strtol(end + 1, NULL, 10);
	if (rep < 1) {
		rep = 1;
	}
	printk(KERN_INFO "stratopi: gpio blink %ld %ld %ld\n", on, off, rep);
	if (on > 0) {
		for (i = 0; i < rep; i++) {
			gpio_set_value(gpio, 1);
			msleep(on);
			gpio_set_value(gpio, 0);
			if (i < rep - 1) {
				msleep(off);
			}
		}
	}
	return count;
}

static void softUartRxCallback(unsigned char character) {
	if (softUartRxBuffIdx < SOFT_UART_RX_BUFF_SIZE - 1) {
		softUartRxBuff[softUartRxBuffIdx++] = character;
	}
}

static bool softUartSendAndWait(const char *cmd, int cmdLen, int respLen) {
	int waitTime = 0;
	raspberry_soft_uart_open(NULL);
	softUartRxBuffIdx = 0;
	printk(KERN_INFO "stratopi: soft uart >>> %s\n", cmd);
	raspberry_soft_uart_send_string(cmd, cmdLen);
	while (softUartRxBuffIdx < respLen && waitTime < SOFT_UART_RX_TIMEOUT) {
		msleep(20);
		waitTime += 20;
	}
	softUartRxBuff[softUartRxBuffIdx] = '\0';
	printk(KERN_INFO "stratopi: soft uart <<< %s\n", softUartRxBuff);
	raspberry_soft_uart_close();
	return softUartRxBuffIdx == respLen;
}

static ssize_t MCU_show(struct device* dev, struct device_attribute* attr,
		char *buf) {
	long val;
	char cmd[] = "XXX??";
	int cmdLen = 4;
	int prefixLen = 3;
	int respLen = getMcuCmd(dev, attr, cmd);
	if (respLen < 0) {
		return -EINVAL;
	}
	if (respLen == 5) {
		cmdLen = 5;
		prefixLen = 4;
	}
	if (!softUartSendAndWait(cmd, cmdLen, respLen)) {
		return -EIO;
	}
	if (kstrtol((const char *) (softUartRxBuff + prefixLen), 10, &val) < 0) {
		return sprintf(buf, "%s\n", softUartRxBuff + prefixLen);
	}
	return sprintf(buf, "%ld\n", val);
}

static ssize_t MCU_store(struct device* dev, struct device_attribute* attr,
		const char *buf, size_t count) {
	int i;
	int padd;
	int prefixLen = 3;
	char cmd[] = "XXX00000";
	int cmdLen = getMcuCmd(dev, attr, cmd);
	if (cmdLen < 0) {
		return -EINVAL;
	}
	if (cmdLen == 5) {
		prefixLen = 4;
	}
	padd = cmdLen - prefixLen - (count - 1);
	if (padd < 0 || padd > 4) {
		return -EINVAL;
	}
	for (i = 0; i < count - 1; i++) {
		cmd[prefixLen + padd + i] = toUpper(buf[i]);
	}
	cmd[prefixLen + padd + i] = '\0';
	if (!softUartSendAndWait(cmd, cmdLen, cmdLen)) {
		return -EIO;
	}
	for (i = 0; i < padd; i++) {
		if (softUartRxBuff[prefixLen + i] != '0') {
			return -EIO;
		}
	}
	for (i = 0; i < count - 1; i++) {
		if (softUartRxBuff[prefixLen + padd + i] != toUpper(buf[i])) {
			return -EIO;
		}
	}
	return count;
}

static struct device_attribute devAttrBuzzerStatus = { //
		.attr = { //
				.name = "status", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrBuzzerBeep = { //
		.attr = { //
				.name = "beep", //
						.mode = 0220, //
				},//
				.show = NULL, //
				.store = GPIOBlink_store, //
		};

static struct device_attribute devAttrWatchdogEnabled = { //
		.attr = { //
				.name = "enabled", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrWatchdogHeartbeat = { //
		.attr = { //
				.name = "heartbeat", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrWatchdogExpired = { //
		.attr = { //
				.name = "expired", //
						.mode = 0440, //
				},//
				.show = GPIO_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrWatchdogEnableMode = { //
		.attr = { //
				.name = "enable_mode", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrWatchdogTimeout = { //
		.attr = { //
				.name = "timeout", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrWatchdogSdSwitch = { //
		.attr = { //
				.name = "sd_switch", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrRs485Mode = { //
		.attr = { //
				.name = "mode", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrRs485Params = { //
		.attr = { //
				.name = "params", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrShutdownEnabled = { //
		.attr = { //
				.name = "enabled", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrShutdownWait = { //
		.attr = { //
				.name = "wait", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrShutdownEnableMode = { //
		.attr = { //
				.name = "enable_mode", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrShutdownDuration = { //
		.attr = { //
				.name = "duration", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrShutdownUpDelay = { //
		.attr = { //
				.name = "delay", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrShutdownUpMode = { //
		.attr = { //
				.name = "up_mode", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrShutdownSdSwitch = { //
		.attr = { //
				.name = "sd_switch", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrUpsBattery = { //
		.attr = { //
				.name = "battery", //
						.mode = 0440, //
				},//
				.show = GPIO_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrUpsPowerOff = { //
		.attr = { //
				.name = "power_off", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrRelayStatus = { //
		.attr = { //
				.name = "status", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrLedStatus = { //
		.attr = { //
				.name = "status", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrLedBlink = { //
		.attr = { //
				.name = "blink", //
						.mode = 0220, //
				},//
				.show = NULL, //
				.store = GPIOBlink_store, //
		};

static struct device_attribute devAttrButtonStatus = { //
		.attr = { //
				.name = "status", //
						.mode = 0440, //
				},//
				.show = GPIO_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrI2CExpEnabled = { //
		.attr = { //
				.name = "enabled", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrI2CExpFeedback = { //
		.attr = { //
				.name = "feedback", //
						.mode = 0440, //
				},//
				.show = GPIO_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrSdSdxEnabled = { //
		.attr = { //
				.name = "sdx_enabled", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrSdSd1Enabled = { //
		.attr = { //
				.name = "sd1_enabled", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrSdSdxRouting = { //
		.attr = { //
				.name = "sdx_routing", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrSdSdxBoot = { //
		.attr = { //
				.name = "sdx_boot", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrMcuConfig = { //
		.attr = { //
				.name = "config", //
						.mode = 0220, //
				},//
				.show = NULL, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrMcuFwVersion = { //
		.attr = { //
				.name = "fw_version", //
						.mode = 0440, //
				},//
				.show = MCU_show, //
				.store = NULL, //
		};

static void cleanup(void) {
	if (pLedDevice && !IS_ERR(pLedDevice)) {
		device_remove_file(pLedDevice, &devAttrLedStatus);
		device_remove_file(pLedDevice, &devAttrLedBlink);

		device_destroy(pDeviceClass, 0);

		gpio_unexport(GPIO_LED);
		gpio_free(GPIO_LED);
	}

	if (pButtonDevice && !IS_ERR(pButtonDevice)) {
		device_remove_file(pButtonDevice, &devAttrButtonStatus);

		device_destroy(pDeviceClass, 0);

		gpio_unexport(GPIO_BUTTON);
		gpio_free(GPIO_BUTTON);
	}

	if (pI2CExpDevice && !IS_ERR(pI2CExpDevice)) {
		device_remove_file(pI2CExpDevice, &devAttrI2CExpEnabled);
		device_remove_file(pI2CExpDevice, &devAttrI2CExpFeedback);

		device_destroy(pDeviceClass, 0);

		gpio_unexport(GPIO_I2CEXP_ENABLE);
		gpio_free(GPIO_I2CEXP_ENABLE);
		gpio_unexport(GPIO_I2CEXP_FEEDBACK);
		gpio_free(GPIO_I2CEXP_FEEDBACK);
	}

	if (pSdDevice && !IS_ERR(pSdDevice)) {
		device_remove_file(pSdDevice, &devAttrSdSdxEnabled);
		device_remove_file(pSdDevice, &devAttrSdSd1Enabled);
		device_remove_file(pSdDevice, &devAttrSdSdxRouting);
		device_remove_file(pSdDevice, &devAttrSdSdxBoot);

		device_destroy(pDeviceClass, 0);
	}

	if (pBuzzerDevice && !IS_ERR(pBuzzerDevice)) {
		device_remove_file(pBuzzerDevice, &devAttrBuzzerStatus);
		device_remove_file(pBuzzerDevice, &devAttrBuzzerBeep);

		device_destroy(pDeviceClass, 0);

		gpio_unexport(GPIO_BUZZER);
		gpio_free(GPIO_BUZZER);
	}

	if (pRelayDevice && !IS_ERR(pRelayDevice)) {
		device_remove_file(pRelayDevice, &devAttrRelayStatus);

		device_destroy(pDeviceClass, 0);

		gpio_unexport(GPIO_RELAY);
		gpio_free(GPIO_RELAY);
	}

	if (pUpsDevice && !IS_ERR(pUpsDevice)) {
		device_remove_file(pUpsDevice, &devAttrUpsBattery);
		device_remove_file(pUpsDevice, &devAttrUpsPowerOff);

		device_destroy(pDeviceClass, 0);

		gpio_unexport(GPIO_UPS_BATTERY);
		gpio_free(GPIO_UPS_BATTERY);
	}

	if (pWatchdogDevice && !IS_ERR(pWatchdogDevice)) {
		device_remove_file(pWatchdogDevice, &devAttrWatchdogEnabled);
		device_remove_file(pWatchdogDevice, &devAttrWatchdogHeartbeat);
		device_remove_file(pWatchdogDevice, &devAttrWatchdogExpired);
		device_remove_file(pWatchdogDevice, &devAttrWatchdogEnableMode);
		device_remove_file(pWatchdogDevice, &devAttrWatchdogTimeout);
		device_remove_file(pWatchdogDevice, &devAttrWatchdogSdSwitch);

		device_destroy(pDeviceClass, 0);
	}

	if (pShutdownDevice && !IS_ERR(pShutdownDevice)) {
		device_remove_file(pShutdownDevice, &devAttrShutdownEnabled);
		device_remove_file(pShutdownDevice, &devAttrShutdownWait);
		device_remove_file(pShutdownDevice, &devAttrShutdownEnableMode);
		device_remove_file(pShutdownDevice, &devAttrShutdownDuration);
		device_remove_file(pShutdownDevice, &devAttrShutdownUpDelay);
		device_remove_file(pShutdownDevice, &devAttrShutdownUpMode);
		device_remove_file(pShutdownDevice, &devAttrShutdownSdSwitch);

		device_destroy(pDeviceClass, 0);
	}

	if (pRs485Device && !IS_ERR(pRs485Device)) {
		device_remove_file(pRs485Device, &devAttrRs485Mode);
		device_remove_file(pRs485Device, &devAttrRs485Params);

		device_destroy(pDeviceClass, 0);
	}

	if (pMcuDevice && !IS_ERR(pMcuDevice)) {
		device_remove_file(pMcuDevice, &devAttrMcuConfig);
		device_remove_file(pMcuDevice, &devAttrMcuFwVersion);

		device_destroy(pDeviceClass, 0);
	}

	if (!IS_ERR(pDeviceClass)) {
		class_destroy(pDeviceClass);
	}

	gpio_unexport(GPIO_WATCHDOG_ENABLE);
	gpio_free(GPIO_WATCHDOG_ENABLE);
	gpio_unexport(GPIO_WATCHDOG_HEARTBEAT);
	gpio_free(GPIO_WATCHDOG_HEARTBEAT);
	gpio_unexport(GPIO_WATCHDOG_EXPIRED);
	gpio_free(GPIO_WATCHDOG_EXPIRED);
	gpio_unexport(GPIO_SHUTDOWN);
	gpio_free(GPIO_SHUTDOWN);

	if (softUartInitialized) {
		if (!raspberry_soft_uart_finalize()) {
			printk(KERN_ALERT "stratopi: error finalizing soft UART\n");;
		}
	}
}

static void setGPIO(void) {
	if (modelNum == MODEL_CM) {
		GPIO_WATCHDOG_ENABLE = 22;
		GPIO_WATCHDOG_HEARTBEAT = 27;
		GPIO_WATCHDOG_EXPIRED = 17;
		GPIO_SHUTDOWN = 18;
		GPIO_LED = 16;
		GPIO_BUTTON = 25;
		GPIO_SOFTSERIAL_TX = 23;
		GPIO_SOFTSERIAL_RX = 24;
	} else if (modelNum == MODEL_CMDUO) {
		GPIO_WATCHDOG_ENABLE = 39;
		GPIO_WATCHDOG_HEARTBEAT = 32;
		GPIO_WATCHDOG_EXPIRED = 17;
		GPIO_SHUTDOWN = 18;
		GPIO_LED = 16;
		GPIO_BUTTON = 38;
		GPIO_SOFTSERIAL_TX = 37;
		GPIO_SOFTSERIAL_RX = 33;
		GPIO_I2CEXP_ENABLE = 6;
		GPIO_I2CEXP_FEEDBACK = 34;
	} else {
		GPIO_BUZZER = 20;
		GPIO_WATCHDOG_ENABLE = 6;
		GPIO_WATCHDOG_HEARTBEAT = 5;
		GPIO_WATCHDOG_EXPIRED = 12;
		GPIO_SHUTDOWN = 16;
		GPIO_UPS_BATTERY = 26;
		GPIO_RELAY = 26;
		GPIO_SOFTSERIAL_TX = 13;
		GPIO_SOFTSERIAL_RX = 19;
	}
}

static bool softUartInit(void) {
	if (!raspberry_soft_uart_init(GPIO_SOFTSERIAL_TX, GPIO_SOFTSERIAL_RX)) {
		return false;
	}
	if (!raspberry_soft_uart_set_baudrate(1200)) {
		raspberry_soft_uart_finalize();
		return false;
	}
	return true;
}

static bool detectModelNumber(void) {
	if (!softUartSendAndWait("XFW?", 4, 10)) {
		return false;
	}
	return (kstrtoint((const char *) (softUartRxBuff + 7), 10, &modelNum) == 0);
}

static bool tryDetectModelNumberAs(int modelTry) {
	modelNum = modelTry;
	setGPIO();
	if (!softUartInit()) {
		return false;
	}
	if (!detectModelNumber()) {
		raspberry_soft_uart_finalize();
		return false;
	}
	return true;
}

static int __init stratopi_init(void) {
	int result = 0;
	softUartInitialized = false;

	printk(KERN_INFO "stratopi: init\n");

	if (!raspberry_soft_uart_set_rx_callback(&softUartRxCallback)) {
		printk(KERN_ALERT "stratopi: error setting soft UART callback\n");
		result = -1;
		goto fail;
	}

	if (strcmp("base", model) == 0) {
		modelNum = MODEL_BASE;
	} else if (strcmp("ups", model) == 0) {
		modelNum = MODEL_UPS;
	} else if (strcmp("can", model) == 0) {
		modelNum = MODEL_CAN;
	} else if (strcmp("cm", model) == 0) {
		modelNum = MODEL_CM;
	} else if (strcmp("cmduo", model) == 0) {
		modelNum = MODEL_CMDUO;
	} else {
		modelNum = -1;
	}

	if (modelNum > 0) {
		setGPIO();
		if (!softUartInit()) {
			printk(KERN_ALERT "stratopi: error initializing soft UART\n");
			result = -1;
			goto fail;
		}
	} else {
		printk(KERN_INFO "stratopi: detecting model\n");
		if (!tryDetectModelNumberAs(MODEL_CMDUO)) {
			if (!tryDetectModelNumberAs(MODEL_CM)) {
				if (!tryDetectModelNumberAs(MODEL_BASE)) {
					printk(KERN_ALERT "stratopi: error detecting model\n");
					result = -1;
					goto fail;
				}
			}
		}
	}

	printk(KERN_INFO "stratopi: model=%d\n", modelNum);

	softUartInitialized = true;

	pDeviceClass = class_create(THIS_MODULE, "stratopi");
	if (IS_ERR(pDeviceClass)) {
		printk(KERN_ALERT "stratopi: failed to create device class\n");
		result = -1;
		goto fail;
	}

	if (modelNum == MODEL_CM || modelNum == MODEL_CMDUO) {
		pLedDevice = device_create(pDeviceClass, NULL, 0, NULL, "led");
		pButtonDevice = device_create(pDeviceClass, NULL, 0, NULL, "button");

		if (IS_ERR(pLedDevice) || IS_ERR(pButtonDevice)) {
			printk(KERN_ALERT "stratopi: failed to create devices\n");
			result = -1;
			goto fail;
		}

		if (modelNum == MODEL_CMDUO) {
			pI2CExpDevice = device_create(pDeviceClass, NULL, 0, NULL, "i2cexp");
			pSdDevice = device_create(pDeviceClass, NULL, 0, NULL, "sd");

			if (IS_ERR(pI2CExpDevice) || IS_ERR(pSdDevice)) {
				printk(KERN_ALERT "stratopi: failed to create devices\n");
				result = -1;
				goto fail;
			}
		}
	} else {
		pBuzzerDevice = device_create(pDeviceClass, NULL, 0, NULL, "buzzer");

		if (IS_ERR(pBuzzerDevice)) {
			printk(KERN_ALERT "stratopi: failed to create devices\n");
			result = -1;
			goto fail;
		}

		if (modelNum == MODEL_CAN) {
			pRelayDevice = device_create(pDeviceClass, NULL, 0, NULL, "relay");

			if (IS_ERR(pRelayDevice)) {
				printk(KERN_ALERT "stratopi: failed to create devices\n");
				result = -1;
				goto fail;
			}

		} else if (modelNum == MODEL_UPS) {
			pUpsDevice = device_create(pDeviceClass, NULL, 0, NULL, "ups");

			if (IS_ERR(pUpsDevice)) {
				printk(KERN_ALERT "stratopi: failed to create devices\n");
				result = -1;
				goto fail;
			}
		}
	}

	pWatchdogDevice = device_create(pDeviceClass, NULL, 0, NULL, "watchdog");
	pShutdownDevice = device_create(pDeviceClass, NULL, 0, NULL, "shutdown");
	pRs485Device = device_create(pDeviceClass, NULL, 0, NULL, "rs485");
	pMcuDevice = device_create(pDeviceClass, NULL, 0, NULL, "mcu");

	if (IS_ERR(pRs485Device) || IS_ERR(pWatchdogDevice) || IS_ERR(pShutdownDevice) || IS_ERR(pMcuDevice)) {
		printk(KERN_ALERT "stratopi: failed to create devices\n");
		result = -1;
		goto fail;
	}

	if (pBuzzerDevice) {
		result |= device_create_file(pBuzzerDevice, &devAttrBuzzerStatus);
		result |= device_create_file(pBuzzerDevice, &devAttrBuzzerBeep);
	}

	if (pWatchdogDevice) {
		result |= device_create_file(pWatchdogDevice, &devAttrWatchdogEnabled);
		result |= device_create_file(pWatchdogDevice, &devAttrWatchdogHeartbeat);
		result |= device_create_file(pWatchdogDevice, &devAttrWatchdogExpired);
		result |= device_create_file(pWatchdogDevice, &devAttrWatchdogEnableMode);
		result |= device_create_file(pWatchdogDevice, &devAttrWatchdogTimeout);
		if (pSdDevice) {
			result |= device_create_file(pWatchdogDevice, &devAttrWatchdogSdSwitch);
		}
	}

	if (pRs485Device) {
		result |= device_create_file(pRs485Device, &devAttrRs485Mode);
		result |= device_create_file(pRs485Device, &devAttrRs485Params);
	}

	if (pShutdownDevice) {
		result |= device_create_file(pShutdownDevice, &devAttrShutdownEnabled);
		result |= device_create_file(pShutdownDevice, &devAttrShutdownWait);
		result |= device_create_file(pShutdownDevice, &devAttrShutdownEnableMode);
		result |= device_create_file(pShutdownDevice, &devAttrShutdownDuration);
		result |= device_create_file(pShutdownDevice, &devAttrShutdownUpDelay);
		result |= device_create_file(pShutdownDevice, &devAttrShutdownUpMode);
		if (pSdDevice) {
			result |= device_create_file(pShutdownDevice, &devAttrShutdownSdSwitch);
		}
	}

	if (pUpsDevice) {
		result |= device_create_file(pUpsDevice, &devAttrUpsBattery);
		result |= device_create_file(pUpsDevice, &devAttrUpsPowerOff);
	}

	if (pRelayDevice) {
		result |= device_create_file(pRelayDevice, &devAttrRelayStatus);
	}

	if (pLedDevice) {
		result |= device_create_file(pLedDevice, &devAttrLedStatus);
		result |= device_create_file(pLedDevice, &devAttrLedBlink);
	}

	if (pButtonDevice) {
		result |= device_create_file(pButtonDevice, &devAttrButtonStatus);
	}

	if (pI2CExpDevice) {
		result |= device_create_file(pI2CExpDevice, &devAttrI2CExpEnabled);
		result |= device_create_file(pI2CExpDevice, &devAttrI2CExpFeedback);
	}

	if (pSdDevice) {
		result |= device_create_file(pSdDevice, &devAttrSdSdxEnabled);
		result |= device_create_file(pSdDevice, &devAttrSdSd1Enabled);
		result |= device_create_file(pSdDevice, &devAttrSdSdxRouting);
		result |= device_create_file(pSdDevice, &devAttrSdSdxBoot);
	}

	if (pMcuDevice) {
		result |= device_create_file(pMcuDevice, &devAttrMcuConfig);
		result |= device_create_file(pMcuDevice, &devAttrMcuFwVersion);
	}

	if (result) {
		printk(KERN_ALERT "stratopi: failed to create device files\n");
		result = -1;
		goto fail;
	}

	if (pBuzzerDevice) {
		gpio_request(GPIO_BUZZER, "stratopi_buzzer");
		result |= gpio_direction_output(GPIO_BUZZER, false);
		gpio_export(GPIO_BUZZER, false);
	}

	gpio_request(GPIO_WATCHDOG_ENABLE, "stratopi_watchdog_enable");
	result |= gpio_direction_output(GPIO_WATCHDOG_ENABLE, false);
	gpio_export(GPIO_WATCHDOG_ENABLE, false);

	gpio_request(GPIO_WATCHDOG_HEARTBEAT, "stratopi_watchdog_heartbeat");
	result |= gpio_direction_output(GPIO_WATCHDOG_HEARTBEAT, false);
	gpio_export(GPIO_WATCHDOG_HEARTBEAT, false);

	gpio_request(GPIO_WATCHDOG_EXPIRED, "stratopi_watchdog_expired");
	result |= gpio_direction_input(GPIO_WATCHDOG_EXPIRED);
	gpio_export(GPIO_WATCHDOG_EXPIRED, false);

	gpio_request(GPIO_SHUTDOWN, "stratopi_shutdown");
	result |= gpio_direction_output(GPIO_SHUTDOWN, false);
	gpio_export(GPIO_SHUTDOWN, false);

	if (pUpsDevice) {
		gpio_request(GPIO_UPS_BATTERY, "stratopi_battery");
		result |= gpio_direction_input(GPIO_UPS_BATTERY);
		gpio_export(GPIO_UPS_BATTERY, false);
	}

	if (pRelayDevice) {
		gpio_request(GPIO_RELAY, "stratopi_relay");
		result |= gpio_direction_output(GPIO_RELAY, false);
		gpio_export(GPIO_RELAY, false);
	}

	if (pLedDevice) {
		gpio_request(GPIO_LED, "stratopi_led");
		result |= gpio_direction_output(GPIO_LED, false);
		gpio_export(GPIO_LED, false);
	}

	if (pButtonDevice) {
		gpio_request(GPIO_BUTTON, "stratopi_button");
		result |= gpio_direction_input(GPIO_BUTTON);
		gpio_export(GPIO_BUTTON, false);
	}

	if (pI2CExpDevice) {
		gpio_request(GPIO_I2CEXP_ENABLE, "stratopi_i2cexp_enable");
		result |= gpio_direction_output(GPIO_I2CEXP_ENABLE, false);
		gpio_export(GPIO_I2CEXP_ENABLE, false);

		gpio_request(GPIO_I2CEXP_FEEDBACK, "stratopi_i2cexp_feedback");
		result |= gpio_direction_input(GPIO_I2CEXP_FEEDBACK);
		gpio_export(GPIO_I2CEXP_FEEDBACK, false);
	}

	if (result) {
		printk(KERN_ALERT "stratopi: error setting up GPIOs\n");
		goto fail;
	}

	printk(KERN_INFO "stratopi: ready\n");
	return 0;

	fail:
	printk(KERN_ALERT "stratopi: init failed\n");
	cleanup();
	return result;
}

static void __exit stratopi_exit(void) {
	cleanup();
	printk(KERN_INFO "stratopi: exit\n");
}

module_init( stratopi_init);
module_exit( stratopi_exit);
