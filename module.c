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

#define MODEL_BASE		1
#define MODEL_UPS		2
#define MODEL_CAN		3
#define MODEL_CM		4
#define MODEL_BASE_3	5
#define MODEL_UPS_3		6
#define MODEL_CMDUO		7

#define SOFT_UART_RX_BUFF_SIZE 	100

#define FW_MAX_SIZE 16000

#define FW_MAX_DATA_BYTES_PER_LINE 0x20
#define FW_MAX_LINE_LEN (FW_MAX_DATA_BYTES_PER_LINE * 2 + 12)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sfera Labs - http://sferalabs.cc");
MODULE_DESCRIPTION("Strato Pi driver module");
MODULE_VERSION("1.0");

static int model_num = -1;
module_param( model_num, int, S_IRUGO);
MODULE_PARM_DESC(model_num, " Strato Pi model number");

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
int GPIO_USB1_DISABLE;
int GPIO_USB1_FAULT;
int GPIO_USB2_DISABLE;
int GPIO_USB2_FAULT;

static struct class *pDeviceClass;

static struct device *pBuzzerDevice = NULL;
static struct device *pWatchdogDevice = NULL;
static struct device *pRs485Device = NULL;
static struct device *pPowerDevice = NULL;
static struct device *pUpsDevice = NULL;
static struct device *pRelayDevice = NULL;
static struct device *pLedDevice = NULL;
static struct device *pButtonDevice = NULL;
static struct device *pExpBusDevice = NULL;
static struct device *pSdDevice = NULL;
static struct device *pUsb1Device = NULL;
static struct device *pUsb2Device = NULL;
static struct device *pMcuDevice = NULL;

static struct device_attribute devAttrBuzzerStatus;
static struct device_attribute devAttrBuzzerBeep;

static struct device_attribute devAttrWatchdogEnabled;
static struct device_attribute devAttrWatchdogHeartbeat;
static struct device_attribute devAttrWatchdogExpired;
static struct device_attribute devAttrWatchdogEnableMode;
static struct device_attribute devAttrWatchdogTimeout;
static struct device_attribute devAttrWatchdogDownDelay;
static struct device_attribute devAttrWatchdogSdSwitch;

static struct device_attribute devAttrRs485Mode;
static struct device_attribute devAttrRs485Params;

static struct device_attribute devAttrPowerDownEnabled;
static struct device_attribute devAttrPowerDownDelay;
static struct device_attribute devAttrPowerDownEnableMode;
static struct device_attribute devAttrPowerOffTime;
static struct device_attribute devAttrPowerUpDelay;
static struct device_attribute devAttrPowerUpMode;
static struct device_attribute devAttrPowerSdSwitch;

static struct device_attribute devAttrUpsBattery;
static struct device_attribute devAttrUpsPowerDelay;

static struct device_attribute devAttrRelayStatus;

static struct device_attribute devAttrLedStatus;
static struct device_attribute devAttrLedBlink;

static struct device_attribute devAttrButtonStatus;

static struct device_attribute devAttrExpBusEnabled;
static struct device_attribute devAttrExpBusAux;

static struct device_attribute devAttrSdSdxEnabled;
static struct device_attribute devAttrSdSd1Enabled;
static struct device_attribute devAttrSdSdxRouting;
static struct device_attribute devAttrSdSdxDefault;

static struct device_attribute devAttrUsb1Disabled;
static struct device_attribute devAttrUsb1Ok;

static struct device_attribute devAttrUsb2Disabled;
static struct device_attribute devAttrUsb2Ok;

static struct device_attribute devAttrMcuConfig;
static struct device_attribute devAttrMcuFwVersion;
static struct device_attribute devAttrMcuFwInstall;
static struct device_attribute devAttrMcuFwInstallProgress;

static DEFINE_MUTEX( mcuMutex);

static bool softUartInitialized;
volatile static char softUartRxBuff[SOFT_UART_RX_BUFF_SIZE];
volatile static int softUartRxBuffIdx;

static int fwVerMaj = 4;
static int fwVerMin = 0;
static uint8_t fwBytes[FW_MAX_SIZE];
static int fwMaxAddr = 0;
static char fwLine[FW_MAX_LINE_LEN];
static int fwLineIdx = 0;
volatile static int fwProgress = 0;

static char toUpper(char c) {
	if (c >= 97 && c <= 122) {
		return c - 32;
	}
	return c;
}

static bool startsWith(const char *str, const char *pre) {
	return strncmp(pre, str, strlen(pre)) == 0;
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
	} else if (dev == pPowerDevice) {
		return GPIO_SHUTDOWN;
	} else if (dev == pUpsDevice) {
		return GPIO_UPS_BATTERY;
	} else if (dev == pRelayDevice) {
		return GPIO_RELAY;
	} else if (dev == pLedDevice) {
		return GPIO_LED;
	} else if (dev == pButtonDevice) {
		return GPIO_BUTTON;
	} else if (dev == pExpBusDevice) {
		if (attr == &devAttrExpBusEnabled) {
			return GPIO_I2CEXP_ENABLE;
		} else if (attr == &devAttrExpBusAux) {
			return GPIO_I2CEXP_FEEDBACK;
		}
	} else if (dev == pUsb1Device) {
		if (attr == &devAttrUsb1Disabled) {
			return GPIO_USB1_DISABLE;
		} else if (attr == &devAttrUsb1Ok) {
			return GPIO_USB1_FAULT;
		}
	} else if (dev == pUsb2Device) {
		if (attr == &devAttrUsb2Disabled) {
			return GPIO_USB2_DISABLE;
		} else if (attr == &devAttrUsb2Ok) {
			return GPIO_USB2_FAULT;
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
	} else if (dev == pPowerDevice) {
		if (attr == &devAttrPowerDownEnableMode) {
			cmd[1] = 'P';
			cmd[2] = 'E';
			return 4;
		} else if (attr == &devAttrPowerDownDelay) {
			cmd[1] = 'P';
			cmd[2] = 'W';
			return 8;
		} else if (attr == &devAttrPowerOffTime) {
			cmd[1] = 'P';
			cmd[2] = 'O';
			return 8;
		} else if (attr == &devAttrPowerUpDelay) {
			cmd[1] = 'P';
			cmd[2] = 'U';
			return 8;
		} else if (attr == &devAttrPowerUpMode) {
			cmd[1] = 'P';
			cmd[2] = 'P';
			return 4;
		} else if (attr == &devAttrPowerSdSwitch) {
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
		} else if (attr == &devAttrWatchdogDownDelay) {
			cmd[1] = 'W';
			cmd[2] = 'W';
			return 8;
		} else if (attr == &devAttrWatchdogSdSwitch) {
			cmd[1] = 'W';
			cmd[2] = 'S';
			cmd[3] = 'D';
			return 5;
		}
	} else if (dev == pUpsDevice) {
		if (attr == &devAttrUpsPowerDelay) {
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
		} else if (attr == &devAttrSdSdxDefault) {
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
			return 9;
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
		if (toUpper(buf[0]) == 'E') { // Enable
			val = true;
		} else if (toUpper(buf[0]) == 'D') { // Disable
			val = false;
		} else if (toUpper(buf[0]) == 'F' || toUpper(buf[0]) == 'T') { // Flip/Toggle
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
	printk(KERN_INFO "stratopi: - | gpio blink %ld %ld %ld\n", on, off, rep);
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
		// printk(KERN_INFO "stratopi: - | soft uart ch %02X\n", (int) (character & 0xff));
		softUartRxBuff[softUartRxBuffIdx++] = character;
	}
}

static bool softUartSendAndWait(const char *cmd, int cmdLen, int respLen,
		int timeout, bool print) {
	int waitTime = 0;
	raspberry_soft_uart_open(NULL);
	softUartRxBuffIdx = 0;
	if (print) {
		printk(KERN_INFO "stratopi: - | soft uart >>> %s\n", cmd);;
	}
	raspberry_soft_uart_send_string(cmd, cmdLen);
	while (softUartRxBuffIdx < respLen && waitTime < timeout) {
		msleep(20);
		waitTime += 20;
	}
	raspberry_soft_uart_close();
	softUartRxBuff[softUartRxBuffIdx] = '\0';
	if (print) {
		printk(KERN_INFO "stratopi: - | soft uart <<< %s\n", softUartRxBuff);;
	}
	return softUartRxBuffIdx == respLen;
}

static ssize_t MCU_show(struct device* dev, struct device_attribute* attr,
		char *buf) {
	long val;
	ssize_t ret;
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

	if (!mutex_trylock(&mcuMutex)) {
		printk(KERN_ALERT "stratopi: * | MCU busy\n");
		return -EBUSY;
	}

	if (!softUartSendAndWait(cmd, cmdLen, respLen, 300, true)) {
		ret = -EIO;
	} else if (kstrtol((const char *) (softUartRxBuff + prefixLen), 10, &val)
			== 0) {
		ret = sprintf(buf, "%ld\n", val);
	} else {
		ret = sprintf(buf, "%s\n", softUartRxBuff + prefixLen);
	}

	mutex_unlock(&mcuMutex);
	return ret;
}

static ssize_t MCU_store(struct device* dev, struct device_attribute* attr,
		const char *buf, size_t count) {
	ssize_t ret = 0;
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

	if (!mutex_trylock(&mcuMutex)) {
		printk(KERN_ALERT "stratopi: * | MCU busy\n");
		return -EBUSY;
	}

	if (!softUartSendAndWait(cmd, cmdLen, cmdLen, 300, true)) {
		ret = -EIO;
	} else {
		for (i = 0; i < padd; i++) {
			if (softUartRxBuff[prefixLen + i] != '0') {
				ret = -EIO;
				break;
			}
		}
		if (ret == 0) {
			for (i = 0; i < count - 1; i++) {
				if (softUartRxBuff[prefixLen + padd + i] != toUpper(buf[i])) {
					ret = -EIO;
					break;
				}
			}
		}
	}
	if (ret == 0) {
		ret = count;
	}
	mutex_unlock(&mcuMutex);
	return ret;
}

static int hex2int(char ch) {
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	return -1;
}

static int nextByte(const char *buf, int offset) {
	int h, l;
	if (offset >= strlen(buf) - 1) {
		printk(KERN_ALERT "stratopi: * | invalid hex file - reached end of line\n");
		return -1;
	}
	h = hex2int(buf[offset]);
	l = hex2int(buf[offset + 1]);
	if (h < 0 || l < 0) {
		printk(KERN_ALERT "stratopi: * | invalid hex file - illegal character\n");
		return -1;
	}
	return (h << 4) | l;
}

static void fwCmdChecksum(char *cmd, int len) {
	int i, checksum = 0;
	for (i = 0; i < len - 2; i++) {
		checksum += (cmd[i] & 0xff);
	}
	checksum = ~checksum;
	cmd[len - 2] = (checksum >> 8) & 0xff;
	cmd[len - 1] = checksum & 0xff;
}

static bool fwSendCmd(int addr, char *cmd, int cmdLen, int respLen,
		const char *respPrefix) {
	cmd[3] = (addr >> 8) & 0xff;
	cmd[4] = addr & 0xff;
	fwCmdChecksum(cmd, 72);
	if (!softUartSendAndWait(cmd, cmdLen, respLen, 1000, false)) {
		printk(KERN_ALERT "stratopi: * | FW cmd error 1\n");
		return false;
	}
	if (!startsWith((const char *) softUartRxBuff, respPrefix)) {
		printk(KERN_ALERT "stratopi: * | FW cmd error 2\n");
		return false;
	}
	return true;
}

static ssize_t fwInstall_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t bufLen) {
	uint8_t data[FW_MAX_DATA_BYTES_PER_LINE];
	int i, buff_i, count, addrH, addrL, addr, type, checksum, baseAddr = 0;
	bool eof = false;
	char *eol;
	char cmd[72 + 1];

	if (!mutex_trylock(&mcuMutex)) {
		printk(KERN_ALERT "stratopi: * | MCU busy\n");
		return -EBUSY;
	}

	fwProgress = 0;

	if (startsWith(buf, ":020000040000FA")) {
		printk(KERN_INFO "stratopi: - | loading firmware file...\n");
		fwLineIdx = 0;
		fwMaxAddr = 0;
		for (i = 0; i < FW_MAX_SIZE; i++) {
			fwBytes[i] = 0xff;
		}
	}

	buff_i = 0;
	while (buff_i < bufLen - 1) {
		if (fwLineIdx == 0 && buf[buff_i++] != ':') {
			continue;
		}

		eol = strchr(buf + buff_i, '\n');
		if (eol == NULL) {
			eol = strchr(buf + buff_i, '\r');
			if (eol == NULL) {
				strcpy(fwLine, buf + buff_i);
				fwLineIdx = bufLen - buff_i;
				printk(KERN_INFO "stratopi: - | waiting for data...\n");
				mutex_unlock(&mcuMutex);
				return bufLen;
			}
		}

		i = (int) (eol - (buf + buff_i));
		strncpy(fwLine + fwLineIdx, buf + buff_i, i);
		fwLine[i + fwLineIdx] = '\0';
		fwLineIdx = 0;

		// printk(KERN_INFO "stratopi: - | line - %s\n", fwLine);

		count = nextByte(fwLine, 0);
		addrH = nextByte(fwLine, 2);
		addrL = nextByte(fwLine, 4);
		type = nextByte(fwLine, 6);
		if (count < 0 || addrH < 0 || addrL < 0 || type < 0) {
			mutex_unlock(&mcuMutex);
			return -EINVAL;
		}
		checksum = count + addrH + addrL + type;
		for (i = 0; i < count; i++) {
			data[i] = nextByte(fwLine, 8 + (i * 2));
			if (data[i] < 0) {
				mutex_unlock(&mcuMutex);
				return -EINVAL;
			}
			checksum += data[i];
		}
		checksum += nextByte(fwLine, 8 + (i * 2));
		if ((checksum & 0xff) != 0) {
			printk(KERN_ALERT "stratopi: * | invalid hex file - checksum error\n");
			mutex_unlock(&mcuMutex);
			return -EINVAL;
		}

		if (type == 0) {
			addr = ((addrH << 8) | addrL) + baseAddr;
			// printk(KERN_INFO "stratopi: - | addr %d\n", addr);
			if (addr + count < FW_MAX_SIZE) {
				for (i = 0; i < count; i++) {
					fwBytes[addr + i] = data[i];
				}
				i = addr + i - 1;
				if (i > fwMaxAddr) {
					fwMaxAddr = i;
				}
			}
		} else if (type == 1) {
			eof = true;
			break;
		} else if (type == 2) {
			baseAddr = ((data[0] << 8) | data[1]) * 16;
		} else if (type == 4) {
			baseAddr = ((data[0] << 8) | data[1]) << 16;
		} else {
			printk(KERN_INFO "stratopi: - | ignored recored type %d\n", type);;
		}
	}

	if (!eof) {
		printk(KERN_INFO "stratopi: - | waiting for data...\n");
		mutex_unlock(&mcuMutex);
		return bufLen;
	}

	if (fwMaxAddr < 0x05be) {
		printk(KERN_ALERT "stratopi: * | invalid hex file - no model\n");
		mutex_unlock(&mcuMutex);
		return -EINVAL;
	}

	if (model_num != fwBytes[0x05be]) {
		printk(KERN_ALERT "stratopi: * | invalid hex file - missmatching model %d != %d\n", model_num, fwBytes[0x05be]);
		mutex_unlock(&mcuMutex);
		return -EINVAL;
	}

	printk(KERN_INFO "stratopi: - | enabling boot loader...\n");
	if (!softUartSendAndWait("XBOOT", 5, 7, 300, true)) {
		printk(KERN_ALERT "stratopi: * | boot loader enable error 1\n");
		mutex_unlock(&mcuMutex);
		return -EIO;
	}
	if (strcmp("XBOOTOK", (const char *) softUartRxBuff) != 0
			&& strcmp("XBOOTIN", (const char *) softUartRxBuff) != 0) {
		printk(KERN_ALERT "stratopi: * | boot loader enable error 2\n");
		mutex_unlock(&mcuMutex);
		return -EIO;
	}
	printk(KERN_INFO "stratopi: - | boot loader enabled\n");

	gpio_set_value(GPIO_SHUTDOWN, 1);

	cmd[0] = 'X';
	cmd[1] = 'B';
	cmd[2] = 'W';
	cmd[5] = 64;
	cmd[72] = '\0';

	fwMaxAddr += 64;

	printk(KERN_INFO "stratopi: - | invalidating FW...\n");
	for (i = 0; i < 64; i++) {
		cmd[6 + i] = 0xff;
	}
	if (!fwSendCmd(0x05C0, cmd, 72, 5, "XBWOK")) {
		mutex_unlock(&mcuMutex);
		return -EIO;
	}

	printk(KERN_INFO "stratopi: - | writing FW...\n");
	for (i = 0; i <= fwMaxAddr - 0x0600; i++) {
		addr = 0x0600 + i;
		cmd[6 + (i % 64)] = fwBytes[addr];
		if (i % 64 == 63) {
			// printk(KERN_INFO "stratopi: - | writing addr %d\n", addr - 63);
			if (!fwSendCmd(addr - 63, cmd, 72, 5, "XBWOK")) {
				mutex_unlock(&mcuMutex);
				return -EIO;
			}
			fwProgress = i * 50 / (fwMaxAddr - 0x0600);
			printk(KERN_INFO "stratopi: - | progress %d%%\n", fwProgress);;
		}
	}

	printk(KERN_INFO "stratopi: - | checking FW...\n");
	cmd[2] = 'R';
	for (i = 0; i <= fwMaxAddr - 0x0600; i++) {
		addr = 0x0600 + i;
		cmd[6 + (i % 64)] = fwBytes[addr];
		if (i % 64 == 63) {
			// printk(KERN_INFO "stratopi: - | reading addr %d\n", addr - 63);
			if (!fwSendCmd(addr - 63, cmd, 6, 72, "XBR")) {
				mutex_unlock(&mcuMutex);
				return -EIO;
			}
			if (memcmp(cmd, (const char *) softUartRxBuff, 72) != 0) {
				printk(KERN_ALERT "stratopi: * | FW check error\n");
				mutex_unlock(&mcuMutex);
				return -EIO;
			}
			fwProgress = 50 + i * 49 / (fwMaxAddr - 0x0600);
			printk(KERN_INFO "stratopi: - | progress %d%%\n", fwProgress);;
		}
	}

	printk(KERN_INFO "stratopi: - | validating FW...\n");
	cmd[2] = 'W';
	for (i = 0; i < 64; i++) {
		cmd[6 + i] = fwBytes[0x05C0 + i];
	}
	if (!fwSendCmd(0x05C0, cmd, 72, 5, "XBWOK")) {
		mutex_unlock(&mcuMutex);
		return -EIO;
	}

	fwProgress = 100;
	printk(KERN_INFO "stratopi: - | progress %d%%\n", fwProgress);

	printk(KERN_INFO "stratopi: - | firmware installed. Waiting for shutdown...\n");

	mutex_unlock(&mcuMutex);
	return bufLen;
}

static ssize_t fwInstallProgress_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	return sprintf(buf, "%d\n", fwProgress);
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

static struct device_attribute devAttrWatchdogDownDelay = { //
		.attr = { //
				.name = "down_delay", //
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

static struct device_attribute devAttrPowerDownEnabled = { //
		.attr = { //
				.name = "down_enabled", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrPowerDownDelay = { //
		.attr = { //
				.name = "down_delay", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrPowerDownEnableMode = { //
		.attr = { //
				.name = "down_enable_mode", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrPowerOffTime = { //
		.attr = { //
				.name = "off_time", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrPowerUpDelay = { //
		.attr = { //
				.name = "up_delay", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrPowerUpMode = { //
		.attr = { //
				.name = "up_mode", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrPowerSdSwitch = { //
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

static struct device_attribute devAttrUpsPowerDelay = { //
		.attr = { //
				.name = "power_delay", //
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

static struct device_attribute devAttrExpBusEnabled = { //
		.attr = { //
				.name = "enabled", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrExpBusAux = { //
		.attr = { //
				.name = "aux", //
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

static struct device_attribute devAttrSdSdxDefault = { //
		.attr = { //
				.name = "sdx_default", //
						.mode = 0660, //
				},//
				.show = MCU_show, //
				.store = MCU_store, //
		};

static struct device_attribute devAttrUsb1Disabled = { //
		.attr = { //
				.name = "disabled", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrUsb1Ok = { //
		.attr = { //
				.name = "ok", //
						.mode = 0440, //
				},//
				.show = GPIO_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrUsb2Disabled = { //
		.attr = { //
				.name = "disabled", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrUsb2Ok = { //
		.attr = { //
				.name = "ok", //
						.mode = 0440, //
				},//
				.show = GPIO_show, //
				.store = NULL, //
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

static struct device_attribute devAttrMcuFwInstall = { //
		.attr = { //
				.name = "fw_install", //
						.mode = 0220, //
				},//
				.show = NULL, //
				.store = fwInstall_store, //
		};

static struct device_attribute devAttrMcuFwInstallProgress = { //
		.attr = { //
				.name = "fw_install_progress", //
						.mode = 0440, //
				},//
				.show = fwInstallProgress_show, //
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

	if (pExpBusDevice && !IS_ERR(pExpBusDevice)) {
		device_remove_file(pExpBusDevice, &devAttrExpBusEnabled);
		device_remove_file(pExpBusDevice, &devAttrExpBusAux);

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
		device_remove_file(pSdDevice, &devAttrSdSdxDefault);

		device_destroy(pDeviceClass, 0);
	}

	if (pUsb1Device && !IS_ERR(pUsb1Device)) {
		device_remove_file(pUsb1Device, &devAttrUsb1Disabled);
		device_remove_file(pUsb1Device, &devAttrUsb1Ok);

		device_destroy(pDeviceClass, 0);

		gpio_unexport(GPIO_USB1_DISABLE);
		gpio_free(GPIO_USB1_DISABLE);
		gpio_unexport(GPIO_USB1_FAULT);
		gpio_free(GPIO_USB1_FAULT);
	}

	if (pUsb2Device && !IS_ERR(pUsb2Device)) {
		device_remove_file(pUsb2Device, &devAttrUsb2Disabled);
		device_remove_file(pUsb2Device, &devAttrUsb2Ok);

		device_destroy(pDeviceClass, 0);

		gpio_unexport(GPIO_USB2_DISABLE);
		gpio_free(GPIO_USB2_DISABLE);
		gpio_unexport(GPIO_USB2_FAULT);
		gpio_free(GPIO_USB2_FAULT);
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
		device_remove_file(pUpsDevice, &devAttrUpsPowerDelay);

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
		device_remove_file(pWatchdogDevice, &devAttrWatchdogDownDelay);
		device_remove_file(pWatchdogDevice, &devAttrWatchdogSdSwitch);

		device_destroy(pDeviceClass, 0);
	}

	if (pPowerDevice && !IS_ERR(pPowerDevice)) {
		device_remove_file(pPowerDevice, &devAttrPowerDownEnabled);
		device_remove_file(pPowerDevice, &devAttrPowerDownDelay);
		device_remove_file(pPowerDevice, &devAttrPowerDownEnableMode);
		device_remove_file(pPowerDevice, &devAttrPowerOffTime);
		device_remove_file(pPowerDevice, &devAttrPowerUpDelay);
		device_remove_file(pPowerDevice, &devAttrPowerUpMode);
		device_remove_file(pPowerDevice, &devAttrPowerSdSwitch);

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
		device_remove_file(pMcuDevice, &devAttrMcuFwInstall);
		device_remove_file(pMcuDevice, &devAttrMcuFwInstallProgress);

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
			printk(KERN_ALERT "stratopi: * | error finalizing soft UART\n");;
		}
	}

	mutex_destroy(&mcuMutex);
}

static void setGPIO(void) {
	if (model_num == MODEL_CM) {
		GPIO_WATCHDOG_ENABLE = 22;
		GPIO_WATCHDOG_HEARTBEAT = 27;
		GPIO_WATCHDOG_EXPIRED = 17;
		GPIO_SHUTDOWN = 18;
		GPIO_LED = 16;
		GPIO_BUTTON = 25;
		GPIO_SOFTSERIAL_TX = 23;
		GPIO_SOFTSERIAL_RX = 24;
	} else if (model_num == MODEL_CMDUO) {
		GPIO_WATCHDOG_ENABLE = 39;
		GPIO_WATCHDOG_HEARTBEAT = 32;
		GPIO_WATCHDOG_EXPIRED = 17;
		GPIO_SHUTDOWN = 18;
		GPIO_LED = 16;
		GPIO_BUTTON = 38;
		GPIO_I2CEXP_ENABLE = 6;
		GPIO_I2CEXP_FEEDBACK = 34;
		GPIO_USB1_DISABLE = 30;
		GPIO_USB1_FAULT = 0;
		GPIO_USB2_DISABLE = 31;
		GPIO_USB2_FAULT = 1;
		GPIO_SOFTSERIAL_TX = 37;
		GPIO_SOFTSERIAL_RX = 33;
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
	msleep(50);
	return true;
}

static bool detectFwVerAndModelNumber(void) {
	char *end = NULL;
	if (!softUartSendAndWait("XFW?", 4, 9, 300, true)
			&& softUartRxBuffIdx < 6) {
		return false;
	}
	fwVerMaj = simple_strtol((const char *) (softUartRxBuff + 3), &end, 10);
	fwVerMin = simple_strtol(end + 1, &end, 10);
	printk(KERN_INFO "stratopi: - | FW version %d.%d\n", fwVerMaj, fwVerMin);
	if (fwVerMaj < 4) {
		printk(KERN_ALERT "stratopi: * | FW version not supported (< 4.0)\n");
		return false;
	}
	return (kstrtoint(end + 1, 10, &model_num) == 0);
}

static bool tryDetectFwAndModelAs(int modelTry) {
	model_num = modelTry;
	setGPIO();
	if (!softUartInit()) {
		return false;
	}
	if (!detectFwVerAndModelNumber()) {
		raspberry_soft_uart_finalize();
		return false;
	}
	return true;
}

static int __init stratopi_init(void) {
	int result = 0;
	softUartInitialized = false;

	printk(KERN_INFO "stratopi: - | init\n");

	mutex_init(&mcuMutex);

	if (!raspberry_soft_uart_set_rx_callback(&softUartRxCallback)) {
		printk(KERN_ALERT "stratopi: * | error setting soft UART callback\n");
		result = -1;
		goto fail;
	}

	if (model_num > 0) {
		setGPIO();
		if (!softUartInit()) {
			printk(KERN_ALERT "stratopi: * | error initializing soft UART\n");
			result = -1;
			goto fail;
		}
	} else {
		printk(KERN_INFO "stratopi: - | detecting model...\n");
		if (!tryDetectFwAndModelAs(MODEL_CMDUO)) {
			if (!tryDetectFwAndModelAs(MODEL_CM)) {
				if (!tryDetectFwAndModelAs(MODEL_BASE)) {
					printk(KERN_ALERT "stratopi: * | error detecting model\n");
					result = -1;
					goto fail;
				}
			}
		}
	}

	printk(KERN_INFO "stratopi: - | model=%d\n", model_num);

	softUartInitialized = true;

	pDeviceClass = class_create(THIS_MODULE, "stratopi");
	if (IS_ERR(pDeviceClass)) {
		printk(KERN_ALERT "stratopi: * | failed to create device class\n");
		result = -1;
		goto fail;
	}

	if (model_num == MODEL_CM || model_num == MODEL_CMDUO) {
		pLedDevice = device_create(pDeviceClass, NULL, 0, NULL, "led");
		pButtonDevice = device_create(pDeviceClass, NULL, 0, NULL, "button");

		if (IS_ERR(pLedDevice) || IS_ERR(pButtonDevice)) {
			printk(KERN_ALERT "stratopi: * | failed to create devices\n");
			result = -1;
			goto fail;
		}

		if (model_num == MODEL_CMDUO) {
			pExpBusDevice = device_create(pDeviceClass, NULL, 0, NULL, "expbus");
			pSdDevice = device_create(pDeviceClass, NULL, 0, NULL, "sd");
			pUsb1Device = device_create(pDeviceClass, NULL, 0, NULL, "usb1");
			pUsb2Device = device_create(pDeviceClass, NULL, 0, NULL, "usb2");

			if (IS_ERR(pExpBusDevice) || IS_ERR(pSdDevice) || IS_ERR(pUsb1Device) || IS_ERR(pUsb2Device)) {
				printk(KERN_ALERT "stratopi: * | failed to create devices\n");
				result = -1;
				goto fail;
			}
		}
	} else {
		pBuzzerDevice = device_create(pDeviceClass, NULL, 0, NULL, "buzzer");

		if (IS_ERR(pBuzzerDevice)) {
			printk(KERN_ALERT "stratopi: * | failed to create devices\n");
			result = -1;
			goto fail;
		}

		if (model_num == MODEL_CAN) {
			pRelayDevice = device_create(pDeviceClass, NULL, 0, NULL, "relay");

			if (IS_ERR(pRelayDevice)) {
				printk(KERN_ALERT "stratopi: * | failed to create devices\n");
				result = -1;
				goto fail;
			}

		} else if (model_num == MODEL_UPS || model_num == MODEL_UPS_3) {
			pUpsDevice = device_create(pDeviceClass, NULL, 0, NULL, "ups");

			if (IS_ERR(pUpsDevice)) {
				printk(KERN_ALERT "stratopi: * | failed to create devices\n");
				result = -1;
				goto fail;
			}
		}
	}

	pWatchdogDevice = device_create(pDeviceClass, NULL, 0, NULL, "watchdog");
	pPowerDevice = device_create(pDeviceClass, NULL, 0, NULL, "power");
	pRs485Device = device_create(pDeviceClass, NULL, 0, NULL, "rs485");
	pMcuDevice = device_create(pDeviceClass, NULL, 0, NULL, "mcu");

	if (IS_ERR(pRs485Device) || IS_ERR(pWatchdogDevice) || IS_ERR(pPowerDevice) || IS_ERR(pMcuDevice)) {
		printk(KERN_ALERT "stratopi: * | failed to create devices\n");
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
		result |= device_create_file(pWatchdogDevice, &devAttrWatchdogDownDelay);
		if (pSdDevice) {
			result |= device_create_file(pWatchdogDevice, &devAttrWatchdogSdSwitch);
		}
	}

	if (pRs485Device) {
		result |= device_create_file(pRs485Device, &devAttrRs485Mode);
		result |= device_create_file(pRs485Device, &devAttrRs485Params);
	}

	if (pPowerDevice) {
		result |= device_create_file(pPowerDevice, &devAttrPowerDownEnabled);
		result |= device_create_file(pPowerDevice, &devAttrPowerDownDelay);
		result |= device_create_file(pPowerDevice, &devAttrPowerDownEnableMode);
		result |= device_create_file(pPowerDevice, &devAttrPowerOffTime);
		result |= device_create_file(pPowerDevice, &devAttrPowerUpDelay);
		if (pUpsDevice) {
			result |= device_create_file(pPowerDevice, &devAttrPowerUpMode);
		}
		if (pSdDevice) {
			result |= device_create_file(pPowerDevice, &devAttrPowerSdSwitch);
		}
	}

	if (pUpsDevice) {
		result |= device_create_file(pUpsDevice, &devAttrUpsBattery);
		result |= device_create_file(pUpsDevice, &devAttrUpsPowerDelay);
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

	if (pExpBusDevice) {
		result |= device_create_file(pExpBusDevice, &devAttrExpBusEnabled);
		result |= device_create_file(pExpBusDevice, &devAttrExpBusAux);
	}

	if (pSdDevice) {
		result |= device_create_file(pSdDevice, &devAttrSdSdxEnabled);
		result |= device_create_file(pSdDevice, &devAttrSdSd1Enabled);
		result |= device_create_file(pSdDevice, &devAttrSdSdxRouting);
		result |= device_create_file(pSdDevice, &devAttrSdSdxDefault);
	}

	if (pUsb1Device) {
		result |= device_create_file(pUsb1Device, &devAttrUsb1Disabled);
		result |= device_create_file(pUsb1Device, &devAttrUsb1Ok);
	}

	if (pUsb2Device) {
		result |= device_create_file(pUsb2Device, &devAttrUsb2Disabled);
		result |= device_create_file(pUsb2Device, &devAttrUsb2Ok);
	}

	if (pMcuDevice) {
		result |= device_create_file(pMcuDevice, &devAttrMcuConfig);
		result |= device_create_file(pMcuDevice, &devAttrMcuFwVersion);
		if (model_num == MODEL_CMDUO || model_num == MODEL_UPS_3 || model_num == MODEL_BASE_3) {
			result |= device_create_file(pMcuDevice, &devAttrMcuFwInstall);
			result |= device_create_file(pMcuDevice, &devAttrMcuFwInstallProgress);
		}
	}

	if (result) {
		printk(KERN_ALERT "stratopi: * | failed to create device files\n");
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

	if (pExpBusDevice) {
		gpio_request(GPIO_I2CEXP_ENABLE, "stratopi_i2cexp_enable");
		result |= gpio_direction_output(GPIO_I2CEXP_ENABLE, false);
		gpio_export(GPIO_I2CEXP_ENABLE, false);

		gpio_request(GPIO_I2CEXP_FEEDBACK, "stratopi_i2cexp_feedback");
		result |= gpio_direction_input(GPIO_I2CEXP_FEEDBACK);
		gpio_export(GPIO_I2CEXP_FEEDBACK, false);
	}

	if (pUsb1Device) {
		gpio_request(GPIO_USB1_DISABLE, "stratopi_usb1_disable");
		result |= gpio_direction_output(GPIO_USB1_DISABLE, false);
		gpio_export(GPIO_USB1_DISABLE, false);

		gpio_request(GPIO_USB1_FAULT, "stratopi_usb1_fault");
		result |= gpio_direction_input(GPIO_USB1_FAULT);
		gpio_export(GPIO_USB1_FAULT, false);
	}

	if (pUsb2Device) {
		gpio_request(GPIO_USB2_DISABLE, "stratopi_usb2_disable");
		result |= gpio_direction_output(GPIO_USB2_DISABLE, false);
		gpio_export(GPIO_USB2_DISABLE, false);

		gpio_request(GPIO_USB2_FAULT, "stratopi_usb2_fault");
		result |= gpio_direction_input(GPIO_USB2_FAULT);
		gpio_export(GPIO_USB2_FAULT, false);
	}

	if (result) {
		printk(KERN_ALERT "stratopi: * | error setting up GPIOs\n");
		goto fail;
	}

	printk(KERN_INFO "stratopi: - | ready\n");
	return 0;

	fail:
	printk(KERN_ALERT "stratopi: * | init failed\n");
	cleanup();
	return result;
}

static void __exit stratopi_exit(void) {
	cleanup();
	printk(KERN_INFO "stratopi: - | exit\n");
}

module_init( stratopi_init);
module_exit( stratopi_exit);
