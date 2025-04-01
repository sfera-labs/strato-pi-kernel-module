/*
 * stratopi
 *
 *     Copyright (C) 2019-2025 Sfera Labs S.r.l.
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
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>

#include "soft_uart/raspberry_soft_uart.h"
#include "atecc/atecc.h"
#include "commons/commons.h"
#include "gpio/gpio.h"

#define MODEL_BASE		1
#define MODEL_UPS		2
#define MODEL_CAN		3
#define MODEL_CM		4
#define MODEL_BASE_3	5
#define MODEL_UPS_3		6
#define MODEL_CMDUO		7
#define MODEL_CAN_2		8
#define MODEL_CM_2		9

#define SOFT_UART_RX_BUFF_SIZE 	100

#define FW_MAX_SIZE 16000

#define FW_MAX_DATA_BYTES_PER_LINE 0x20
#define FW_MAX_LINE_LEN (FW_MAX_DATA_BYTES_PER_LINE * 2 + 12)

#define LOG_TAG "stratopi: "

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sfera Labs - http://sferalabs.cc");
MODULE_DESCRIPTION("Strato Pi driver module");
MODULE_VERSION("1.23");

static int model_num = -1;
module_param( model_num, int, S_IRUGO);
MODULE_PARM_DESC(model_num, " Strato Pi model number");

static int model_num_fallback = -1;
module_param( model_num_fallback, int, S_IRUGO);
MODULE_PARM_DESC(model_num_fallback, " Strato Pi model number auto-detect fail fallback");

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
static struct device *pSecElDevice = NULL;

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
static struct device_attribute devAttrButtonStatusDeb;
static struct device_attribute devAttrButtonStatusDebMs;
static struct device_attribute devAttrButtonStatusDebCnt;

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
static struct device_attribute devAttrSecElSerialNum;

static const char *stratopi_gp22 = "stratopi_gp22";
static const char *stratopi_gp27 = "stratopi_gp27";
static const char *stratopi_gp17 = "stratopi_gp17";
static const char *stratopi_gp18 = "stratopi_gp18";
static const char *stratopi_gp16 = "stratopi_gp16";
static const char *stratopi_gp25 = "stratopi_gp25";
static const char *stratopi_gp23 = "stratopi_gp23";
static const char *stratopi_gp24 = "stratopi_gp24";
static const char *stratopi_gp39 = "stratopi_gp39";
static const char *stratopi_gp32 = "stratopi_gp32";
static const char *stratopi_gp38 = "stratopi_gp38";
static const char *stratopi_gp6 = "stratopi_gp6";
static const char *stratopi_gp34 = "stratopi_gp34";
static const char *stratopi_gp30 = "stratopi_gp30";
static const char *stratopi_gp0 = "stratopi_gp0";
static const char *stratopi_gp31 = "stratopi_gp31";
static const char *stratopi_gp1 = "stratopi_gp1";
static const char *stratopi_gp37 = "stratopi_gp37";
static const char *stratopi_gp33 = "stratopi_gp33";
static const char *stratopi_gp20 = "stratopi_gp20";
static const char *stratopi_gp5 = "stratopi_gp5";
static const char *stratopi_gp12 = "stratopi_gp12";
static const char *stratopi_gp26 = "stratopi_gp26";
static const char *stratopi_gp13 = "stratopi_gp13";
static const char *stratopi_gp19 = "stratopi_gp19";

static DEFINE_MUTEX(mcuMutex);

static struct GpioBean gpioBuzzer = {
	.flags = GPIOD_OUT_LOW,
};

static struct GpioBean gpioWatchdogEnable = {
	.flags = GPIOD_OUT_LOW,
};

static struct GpioBean gpioWatchdogHeartbeat = {
	.flags = GPIOD_OUT_LOW,
};

static struct DebouncedGpioBean gpioWatchdogExpired = {
	.gpio = {
		.flags = GPIOD_IN,
	},
};

static struct GpioBean gpioShutdown = {
	.flags = GPIOD_OUT_LOW,
};

static struct DebouncedGpioBean gpioUpsBattery = {
	.gpio = {
		.flags = GPIOD_IN,
	},
};

static struct GpioBean gpioRelay = {
	.flags = GPIOD_OUT_LOW,
};

static struct GpioBean gpioLed = {
	.flags = GPIOD_OUT_LOW,
};

static struct DebouncedGpioBean gpioButton = {
	.gpio = {
		.flags = GPIOD_IN,
	},
};

static struct GpioBean gpioI2cExpEnable = {
	.flags = GPIOD_OUT_LOW,
};

static struct GpioBean gpioI2cExpFeedback = {
	.flags = GPIOD_IN,
};

static struct GpioBean gpioUsb1Disable = {
	.flags = GPIOD_OUT_LOW,
};

static struct GpioBean gpioUsb1Fault = {
	.flags = GPIOD_IN,
};

static struct GpioBean gpioUsb2Disable = {
	.flags = GPIOD_OUT_LOW,
};

static struct GpioBean gpioUsb2Fault = {
	.flags = GPIOD_IN,
};

static struct GpioBean gpioSoftSerTx = {
	.flags = GPIOD_OUT_LOW,
};

static struct GpioBean gpioSoftSerRx = {
	.flags = GPIOD_IN,
};

static bool softUartInitialized;
static volatile char softUartRxBuff[SOFT_UART_RX_BUFF_SIZE];
static volatile int softUartRxBuffIdx;

static int fwVerMaj = 4;
static int fwVerMin = 0;
static uint8_t fwBytes[FW_MAX_SIZE];
static int fwMaxAddr = 0;
static char fwLine[FW_MAX_LINE_LEN];
static int fwLineIdx = 0;
static volatile int fwProgress = 0;

static bool startsWith(const char *str, const char *pre) {
	return strncmp(pre, str, strlen(pre)) == 0;
}

static bool mcuMutexLock(void) {
	uint8_t i;
	for (i = 0; i < 20; i++) {
		if (mutex_trylock(&mcuMutex)) {
			return true;
		}
		msleep(1);
	}
	return false;
}

struct GpioBean* gpioGetBean(struct device *dev, struct device_attribute *attr) {
	if (dev == pBuzzerDevice) {
		return &gpioBuzzer;
	} else if (dev == pWatchdogDevice) {
		if (attr == &devAttrWatchdogEnabled) {
			return &gpioWatchdogEnable;
		} else if (attr == &devAttrWatchdogHeartbeat) {
			return &gpioWatchdogHeartbeat;
		} else if (attr == &devAttrWatchdogExpired) {
			return &gpioWatchdogExpired.gpio;
		}
	} else if (dev == pPowerDevice) {
		return &gpioShutdown;
	} else if (dev == pUpsDevice) {
		return &gpioUpsBattery.gpio;
	} else if (dev == pRelayDevice) {
		return &gpioRelay;
	} else if (dev == pLedDevice) {
		return &gpioLed;
	} else if (dev == pButtonDevice) {
		return &gpioButton.gpio;
	} else if (dev == pExpBusDevice) {
		if (attr == &devAttrExpBusEnabled) {
			return &gpioI2cExpEnable;
		} else if (attr == &devAttrExpBusAux) {
			return &gpioI2cExpFeedback;
		}
	} else if (dev == pUsb1Device) {
		if (attr == &devAttrUsb1Disabled) {
			return &gpioUsb1Disable;
		} else if (attr == &devAttrUsb1Ok) {
			return &gpioUsb1Fault;
		}
	} else if (dev == pUsb2Device) {
		if (attr == &devAttrUsb2Disabled) {
			return &gpioUsb2Disable;
		} else if (attr == &devAttrUsb2Ok) {
			return &gpioUsb2Fault;
		}
	}
	return NULL;
}

static int getMcuCmd(struct device *dev, struct device_attribute *attr,
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
			return fwVerMaj == 3 ? 6 : 9;
		}
	}
	return -1;
}

static void softUartRxCallback(unsigned char character) {
	if (softUartRxBuffIdx < SOFT_UART_RX_BUFF_SIZE - 1) {
		softUartRxBuff[softUartRxBuffIdx++] = character;
	}
}

static bool softUartSendAndWait(const char *cmd, int cmdLen, int respLen,
		int timeout, bool print) {
	int i, waitTime;
	for (i = 0; i < 3; i++) {
		waitTime = 0;
		raspberry_soft_uart_open(NULL);
		softUartRxBuffIdx = 0;
		if (print) {
			pr_info(LOG_TAG "soft uart >>> %s\n", cmd);
		}
		raspberry_soft_uart_send_string(cmd, cmdLen);
		while (softUartRxBuffIdx < respLen && waitTime < timeout) {
			msleep(20);
			waitTime += 20;
		}
		raspberry_soft_uart_close();
		softUartRxBuff[softUartRxBuffIdx] = '\0';
		if (print) {
			pr_info(LOG_TAG "soft uart <<< %s\n", softUartRxBuff);
		}
		if (softUartRxBuffIdx == respLen) {
			return true;
		}
		msleep(50);
	}
	return false;
}

static ssize_t MCU_show(struct device *dev, struct device_attribute *attr,
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

	if (!mcuMutexLock()) {
		return -EBUSY;
	}

	if (!softUartSendAndWait(cmd, cmdLen, respLen, 300, false)) {
		ret = -EIO;
	} else if (kstrtol((const char*) (softUartRxBuff + prefixLen), 10, &val)
			== 0) {
		ret = sprintf(buf, "%ld\n", val);
	} else {
		ret = sprintf(buf, "%s\n", softUartRxBuff + prefixLen);
	}

	mutex_unlock(&mcuMutex);
	return ret;
}

static ssize_t MCU_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count) {
	ssize_t ret = count;
	size_t len = count;
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
	while (len > 0
			&& (buf[len - 1] == '\n' || buf[len - 1] == '\r'
					|| buf[len - 1] == ' ')) {
		len--;
	}
	if (len < 1) {
		return -EINVAL;
	}
	padd = cmdLen - prefixLen - len;
	if (padd < 0 || padd > 4) {
		return -EINVAL;
	}
	for (i = 0; i < len; i++) {
		cmd[prefixLen + padd + i] = toUpper(buf[i]);
	}
	cmd[prefixLen + padd + i] = '\0';

	if (!mcuMutexLock()) {
		return -EBUSY;
	}

	if (!softUartSendAndWait(cmd, cmdLen, cmdLen, 300, false)) {
		ret = -EIO;
	} else {
		for (i = 0; i < padd; i++) {
			if (softUartRxBuff[prefixLen + i] != '0') {
				ret = -EIO;
				break;
			}
		}
		if (ret == count) {
			for (i = 0; i < len; i++) {
				if (softUartRxBuff[prefixLen + padd + i] != toUpper(buf[i])) {
					ret = -EIO;
					break;
				}
			}
		}
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
		pr_err(LOG_TAG "invalid hex file - reached end of line\n");
		return -1;
	}
	h = hex2int(buf[offset]);
	l = hex2int(buf[offset + 1]);
	if (h < 0 || l < 0) {
		pr_err(LOG_TAG "invalid hex file - illegal character\n");
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
		pr_err(LOG_TAG "FW cmd error 1\n");
		return false;
	}
	if (!startsWith((const char*) softUartRxBuff, respPrefix)) {
		pr_err(LOG_TAG "FW cmd error 2\n");
		return false;
	}
	return true;
}

static ssize_t fwInstall_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t bufLen) {
	uint8_t data[FW_MAX_DATA_BYTES_PER_LINE];
	int i, buff_i, count, addrH, addrL, addr, type, checksum, baseAddr = 0;
	bool eof = false;
	char *eol;
	char cmd[72 + 1];

	if (!mcuMutexLock()) {
		return -EBUSY;
	}

	fwProgress = 0;

	if (startsWith(buf, ":020000040000FA")) {
		pr_info(LOG_TAG "loading firmware file...\n");
		fwLineIdx = 0;
		fwMaxAddr = 0;
		for (i = 0; i < FW_MAX_SIZE; i++) {
			fwBytes[i] = 0xff;
		}
	}

	buff_i = 0;
	while (buff_i < bufLen) {
		if (fwLineIdx == 0 && buf[buff_i++] != ':') {
			continue;
		}

		eol = strchr(buf + buff_i, '\n');
		if (eol == NULL) {
			eol = strchr(buf + buff_i, '\r');
			if (eol == NULL) {
				strcpy(fwLine, buf + buff_i);
				fwLineIdx = bufLen - buff_i;
				pr_info(LOG_TAG "waiting for data...\n");
				mutex_unlock(&mcuMutex);
				return bufLen;
			}
		}

		i = (int) (eol - (buf + buff_i));
		strncpy(fwLine + fwLineIdx, buf + buff_i, i);
		fwLine[i + fwLineIdx] = '\0';
		fwLineIdx = 0;

		// pr_info(LOG_TAG "line - %s\n", fwLine);

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
			pr_err(LOG_TAG "invalid hex file - checksum error\n");
			mutex_unlock(&mcuMutex);
			return -EINVAL;
		}

		if (type == 0) {
			addr = ((addrH << 8) | addrL) + baseAddr;
			// pr_info(LOG_TAG "addr %d\n", addr);
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
			pr_info(LOG_TAG "ignored record type %d\n", type);
		}
	}

	if (!eof) {
		pr_info(LOG_TAG "waiting for data...\n");
		mutex_unlock(&mcuMutex);
		return bufLen;
	}

	if (fwMaxAddr < 0x05be) {
		pr_err(LOG_TAG "invalid hex file - no model\n");
		mutex_unlock(&mcuMutex);
		return -EINVAL;
	}

	if (model_num != fwBytes[0x05be]) {
		pr_err(LOG_TAG "invalid hex file - missmatching model %d != %d\n",
				model_num, fwBytes[0x05be]);
		mutex_unlock(&mcuMutex);
		return -EINVAL;
	}

	pr_info(LOG_TAG "enabling boot loader...\n");
	if (!softUartSendAndWait("XBOOT", 5, 7, 300, true)) {
		pr_err(LOG_TAG "boot loader enable error 1\n");
		mutex_unlock(&mcuMutex);
		return -EIO;
	}
	if (strcmp("XBOOTOK", (const char*) softUartRxBuff) != 0
			&& strcmp("XBOOTIN", (const char*) softUartRxBuff) != 0) {
		pr_err(LOG_TAG "boot loader enable error 2\n");
		mutex_unlock(&mcuMutex);
		return -EIO;
	}
	pr_info(LOG_TAG "boot loader enabled\n");

	gpioSetVal(&gpioShutdown, 1);

	cmd[0] = 'X';
	cmd[1] = 'B';
	cmd[2] = 'W';
	cmd[5] = 64;
	cmd[72] = '\0';

	fwMaxAddr += 64;

	pr_info(LOG_TAG "invalidating FW...\n");
	for (i = 0; i < 64; i++) {
		cmd[6 + i] = 0xff;
	}
	if (!fwSendCmd(0x05C0, cmd, 72, 5, "XBWOK")) {
		mutex_unlock(&mcuMutex);
		return -EIO;
	}

	pr_info(LOG_TAG "writing FW...\n");
	for (i = 0; i <= fwMaxAddr - 0x0600; i++) {
		addr = 0x0600 + i;
		cmd[6 + (i % 64)] = fwBytes[addr];
		if (i % 64 == 63) {
			// pr_info(LOG_TAG "writing addr %d\n", addr - 63);
			if (!fwSendCmd(addr - 63, cmd, 72, 5, "XBWOK")) {
				mutex_unlock(&mcuMutex);
				return -EIO;
			}
			fwProgress = i * 50 / (fwMaxAddr - 0x0600);
			pr_info(LOG_TAG "progress %d%%\n", fwProgress);
		}
	}

	pr_info(LOG_TAG "checking FW...\n");
	cmd[2] = 'R';
	for (i = 0; i <= fwMaxAddr - 0x0600; i++) {
		addr = 0x0600 + i;
		cmd[6 + (i % 64)] = fwBytes[addr];
		if (i % 64 == 63) {
			// pr_info(LOG_TAG "reading addr %d\n", addr - 63);
			if (!fwSendCmd(addr - 63, cmd, 6, 72, "XBR")) {
				mutex_unlock(&mcuMutex);
				return -EIO;
			}
			if (memcmp(cmd, (const char*) softUartRxBuff, 72) != 0) {
				pr_err(LOG_TAG "FW check error\n");
				mutex_unlock(&mcuMutex);
				return -EIO;
			}
			fwProgress = 50 + i * 49 / (fwMaxAddr - 0x0600);
			pr_info(LOG_TAG "progress %d%%\n", fwProgress);
		}
	}

	pr_info(LOG_TAG "validating FW...\n");
	cmd[2] = 'W';
	for (i = 0; i < 64; i++) {
		cmd[6 + i] = fwBytes[0x05C0 + i];
	}
	if (!fwSendCmd(0x05C0, cmd, 72, 5, "XBWOK")) {
		mutex_unlock(&mcuMutex);
		return -EIO;
	}

	fwProgress = 100;
	pr_info(LOG_TAG "progress %d%%\n", fwProgress);

	pr_info(LOG_TAG "firmware installed. Waiting for shutdown...\n");

	mutex_unlock(&mcuMutex);
	return bufLen;
}

static ssize_t fwInstallProgress_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	return sprintf(buf, "%d\n", fwProgress);
}

static struct device_attribute devAttrBuzzerStatus = {
	.attr = {
		.name = "status",
		.mode = 0660,
	},
	.show = devAttrGpio_show,
	.store = devAttrGpio_store,
};

static struct device_attribute devAttrBuzzerBeep = {
	.attr = {
		.name = "beep",
		.mode = 0220,
	},
	.show = NULL,
	.store = devAttrGpioBlink_store,
};

static struct device_attribute devAttrWatchdogEnabled = {
	.attr = {
		.name = "enabled",
		.mode = 0660,
	},
	.show = devAttrGpio_show,
	.store = devAttrGpio_store,
};

static struct device_attribute devAttrWatchdogHeartbeat = {
	.attr = {
		.name = "heartbeat",
		.mode = 0660,
	},
	.show = devAttrGpio_show,
	.store = devAttrGpio_store,
};

static struct device_attribute devAttrWatchdogExpired = {
	.attr = {
		.name = "expired",
		.mode = 0440,
	},
	.show = devAttrGpioDeb_show,
	.store = NULL,
};

static struct device_attribute devAttrWatchdogEnableMode = {
	.attr = {
		.name = "enable_mode",
		.mode = 0660,
	},
	.show = MCU_show,
	.store = MCU_store,
};

static struct device_attribute devAttrWatchdogTimeout = {
	.attr = {
		.name = "timeout",
		.mode = 0660,
	},
	.show = MCU_show,
	.store = MCU_store,
};

static struct device_attribute devAttrWatchdogDownDelay = {
	.attr = {
		.name = "down_delay",
		.mode = 0660,
	},
	.show = MCU_show,
	.store = MCU_store,
};

static struct device_attribute devAttrWatchdogSdSwitch = {
	.attr = {
		.name = "sd_switch",
		.mode = 0660,
	},
	.show = MCU_show,
	.store = MCU_store,
};

static struct device_attribute devAttrRs485Mode = {
	.attr = {
		.name = "mode",
		.mode = 0660,
	},
	.show = MCU_show,
	.store = MCU_store,
};

static struct device_attribute devAttrRs485Params = {
	.attr = {
		.name = "params",
		.mode = 0660,
	},
	.show = MCU_show,
	.store = MCU_store,
};

static struct device_attribute devAttrPowerDownEnabled = {
	.attr = {
		.name = "down_enabled",
		.mode = 0660,
	},
	.show = devAttrGpio_show,
	.store = devAttrGpio_store,
};

static struct device_attribute devAttrPowerDownDelay = {
	.attr = {
		.name = "down_delay",
		.mode = 0660,
	},
	.show = MCU_show,
	.store = MCU_store,
};

static struct device_attribute devAttrPowerDownEnableMode = {
	.attr = {
		.name = "down_enable_mode",
		.mode = 0660,
	},
	.show = MCU_show,
	.store = MCU_store,
};

static struct device_attribute devAttrPowerOffTime = {
	.attr = {
		.name = "off_time",
		.mode = 0660,
	},
	.show = MCU_show,
	.store = MCU_store,
};

static struct device_attribute devAttrPowerUpDelay = {
	.attr = {
		.name = "up_delay",
		.mode = 0660,
	},
	.show = MCU_show,
	.store = MCU_store,
};

static struct device_attribute devAttrPowerUpMode = {
	.attr = {
		.name = "up_mode",
		.mode = 0660,
	},
	.show = MCU_show,
	.store = MCU_store,
};

static struct device_attribute devAttrPowerSdSwitch = {
	.attr = {
		.name = "sd_switch",
		.mode = 0660,
	},
	.show = MCU_show,
	.store = MCU_store,
};

static struct device_attribute devAttrUpsBattery = {
	.attr = {
		.name = "battery",
		.mode = 0440,
	},
	.show = devAttrGpioDeb_show,
	.store = NULL,
};

static struct device_attribute devAttrUpsPowerDelay = {
	.attr = {
		.name = "power_delay",
		.mode = 0660,
	},
	.show = MCU_show,
	.store = MCU_store,
};

static struct device_attribute devAttrRelayStatus = {
	.attr = {
		.name = "status",
		.mode = 0660,
	},
	.show = devAttrGpio_show,
	.store = devAttrGpio_store,
};

static struct device_attribute devAttrLedStatus = {
	.attr = {
		.name = "status",
		.mode = 0660,
	},
	.show = devAttrGpio_show,
	.store = devAttrGpio_store,
};

static struct device_attribute devAttrLedBlink = {
	.attr = {
		.name = "blink",
		.mode = 0220,
	},
	.show = NULL,
	.store = devAttrGpioBlink_store,
};

static struct device_attribute devAttrButtonStatus = {
	.attr = {
		.name = "status",
		.mode = 0440,
	},
	.show = devAttrGpio_show,
	.store = NULL,
};

static struct device_attribute devAttrButtonStatusDeb = {
	.attr = {
		.name = "status_deb",
		.mode = 0440,
	},
	.show = devAttrGpioDeb_show,
	.store = NULL,
};

static struct device_attribute devAttrButtonStatusDebMs = {
	.attr = {
		.name = "status_deb_ms",
		.mode = 0660,
	},
	.show = devAttrGpioDebMsOn_show,
	.store = devAttrGpioDebMsOn_store,
};

static struct device_attribute devAttrButtonStatusDebCnt = {
	.attr = {
		.name = "status_deb_cnt",
		.mode = 0440,
	},
	.show = devAttrGpioDebOnCnt_show,
	.store = NULL,
};

static struct device_attribute devAttrExpBusEnabled = {
	.attr = {
		.name = "enabled",
		.mode = 0660,
	},
	.show = devAttrGpio_show,
	.store = devAttrGpio_store,
};

static struct device_attribute devAttrExpBusAux = {
	.attr = {
		.name = "aux",
		.mode = 0440,
	},
	.show = devAttrGpio_show,
	.store = NULL,
};

static struct device_attribute devAttrSdSdxEnabled = {
	.attr = {
		.name = "sdx_enabled",
		.mode = 0660,
	},
	.show = MCU_show,
	.store = MCU_store,
};

static struct device_attribute devAttrSdSd1Enabled = {
	.attr = {
		.name = "sd1_enabled",
		.mode = 0660,
	},
	.show = MCU_show,
	.store = MCU_store,
};

static struct device_attribute devAttrSdSdxRouting = {
	.attr = {
		.name = "sdx_routing",
		.mode = 0660,
	},
	.show = MCU_show,
	.store = MCU_store,
};

static struct device_attribute devAttrSdSdxDefault = {
	.attr = {
		.name = "sdx_default",
		.mode = 0660,
	},
	.show = MCU_show,
	.store = MCU_store,
};

static struct device_attribute devAttrUsb1Disabled = {
	.attr = {
		.name = "disabled",
		.mode = 0660,
	},
	.show = devAttrGpio_show,
	.store = devAttrGpio_store,
};

static struct device_attribute devAttrUsb1Ok = {
	.attr = {
		.name = "ok",
		.mode = 0440,
	},
	.show = devAttrGpio_show,
	.store = NULL,
};

static struct device_attribute devAttrUsb2Disabled = {
	.attr = {
		.name = "disabled",
		.mode = 0660,
	},
	.show = devAttrGpio_show,
	.store = devAttrGpio_store,
};

static struct device_attribute devAttrUsb2Ok = {
	.attr = {
		.name = "ok",
		.mode = 0440,
	},
	.show = devAttrGpio_show,
	.store = NULL,
};

static struct device_attribute devAttrMcuConfig = {
	.attr = {
		.name = "config",
		.mode = 0220,
	},
	.show = NULL,
	.store = MCU_store,
};

static struct device_attribute devAttrMcuFwVersion = {
	.attr = {
		.name = "fw_version",
		.mode = 0440,
	},
	.show = MCU_show,
	.store = NULL,
};

static struct device_attribute devAttrMcuFwInstall = {
	.attr = {
		.name = "fw_install",
		.mode = 0220,
	},
	.show = NULL,
	.store = fwInstall_store,
};

static struct device_attribute devAttrMcuFwInstallProgress = {
	.attr = {
		.name = "fw_install_progress",
		.mode = 0440,
	},
	.show = fwInstallProgress_show,
	.store = NULL,
};

static struct device_attribute devAttrSecElSerialNum = {
	.attr = {
		.name = "serial_num",
		.mode = 0440,
	},
	.show = devAttrAteccSerial_show,
	.store = NULL,
};

static void cleanup(void) {
	if (pLedDevice && !IS_ERR(pLedDevice)) {
		device_remove_file(pLedDevice, &devAttrLedStatus);
		device_remove_file(pLedDevice, &devAttrLedBlink);

		device_destroy(pDeviceClass, 0);

		gpioFree(&gpioLed);
	}

	if (pButtonDevice && !IS_ERR(pButtonDevice)) {
		device_remove_file(pButtonDevice, &devAttrButtonStatus);
		device_remove_file(pButtonDevice, &devAttrButtonStatusDeb);
		device_remove_file(pButtonDevice, &devAttrButtonStatusDebMs);
		device_remove_file(pButtonDevice, &devAttrButtonStatusDebCnt);

		device_destroy(pDeviceClass, 0);

		gpioFreeDebounce(&gpioButton);
	}

	if (pExpBusDevice && !IS_ERR(pExpBusDevice)) {
		device_remove_file(pExpBusDevice, &devAttrExpBusEnabled);
		device_remove_file(pExpBusDevice, &devAttrExpBusAux);

		device_destroy(pDeviceClass, 0);

		gpioFree(&gpioI2cExpEnable);
		gpioFree(&gpioI2cExpFeedback);
	}

	if (pUsb1Device && !IS_ERR(pUsb1Device)) {
		device_remove_file(pUsb1Device, &devAttrUsb1Disabled);
		device_remove_file(pUsb1Device, &devAttrUsb1Ok);

		device_destroy(pDeviceClass, 0);

		gpioFree(&gpioUsb1Disable);
		gpioFree(&gpioUsb1Fault);
	}

	if (pUsb2Device && !IS_ERR(pUsb2Device)) {
		device_remove_file(pUsb2Device, &devAttrUsb2Disabled);
		device_remove_file(pUsb2Device, &devAttrUsb2Ok);

		device_destroy(pDeviceClass, 0);

		gpioFree(&gpioUsb2Disable);
		gpioFree(&gpioUsb2Fault);
	}

	if (pSdDevice && !IS_ERR(pSdDevice)) {
		device_remove_file(pSdDevice, &devAttrSdSdxEnabled);
		device_remove_file(pSdDevice, &devAttrSdSd1Enabled);
		device_remove_file(pSdDevice, &devAttrSdSdxRouting);
		device_remove_file(pSdDevice, &devAttrSdSdxDefault);

		device_destroy(pDeviceClass, 0);
	}

	if (pBuzzerDevice && !IS_ERR(pBuzzerDevice)) {
		device_remove_file(pBuzzerDevice, &devAttrBuzzerStatus);
		device_remove_file(pBuzzerDevice, &devAttrBuzzerBeep);

		device_destroy(pDeviceClass, 0);

		gpioFree(&gpioBuzzer);
	}

	if (pRelayDevice && !IS_ERR(pRelayDevice)) {
		device_remove_file(pRelayDevice, &devAttrRelayStatus);

		device_destroy(pDeviceClass, 0);

		gpioFree(&gpioRelay);
	}

	if (pUpsDevice && !IS_ERR(pUpsDevice)) {
		device_remove_file(pUpsDevice, &devAttrUpsBattery);
		device_remove_file(pUpsDevice, &devAttrUpsPowerDelay);

		device_destroy(pDeviceClass, 0);

		gpioFreeDebounce(&gpioUpsBattery);
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

	if (pSecElDevice && !IS_ERR(pSecElDevice)) {
		device_remove_file(pSecElDevice, &devAttrSecElSerialNum);

		device_destroy(pDeviceClass, 0);
	}

	if (!IS_ERR(pDeviceClass)) {
		class_destroy(pDeviceClass);
	}

	gpioFree(&gpioWatchdogEnable);
	gpioFree(&gpioWatchdogHeartbeat);
	gpioFreeDebounce(&gpioWatchdogExpired);
	gpioFree(&gpioShutdown);

	if (softUartInitialized) {
		if (!raspberry_soft_uart_finalize()) {
			pr_err(LOG_TAG "error finalizing soft UART\n");
		}
	}

	mutex_destroy(&mcuMutex);
}

static void setGPIO(void) {
	if (model_num == MODEL_CM) {
		gpioWatchdogEnable.name = stratopi_gp22;
		gpioWatchdogHeartbeat.name = stratopi_gp27;
		gpioWatchdogExpired.gpio.name = stratopi_gp17;
		gpioShutdown.name = stratopi_gp18;
		gpioLed.name = stratopi_gp16;
		gpioButton.gpio.name = stratopi_gp25;
		gpioSoftSerTx.name = stratopi_gp23;
		gpioSoftSerRx.name = stratopi_gp24;
	} else if (model_num == MODEL_CMDUO || model_num == MODEL_CM_2) {
		gpioWatchdogEnable.name = stratopi_gp39;
		gpioWatchdogHeartbeat.name = stratopi_gp32;
		gpioWatchdogExpired.gpio.name = stratopi_gp17;
		gpioShutdown.name = stratopi_gp18;
		gpioLed.name = stratopi_gp16;
		gpioButton.gpio.name = stratopi_gp38;
		gpioI2cExpEnable.name = stratopi_gp6;
		gpioI2cExpFeedback.name = stratopi_gp34;
		gpioUsb1Disable.name = stratopi_gp30;
		gpioUsb1Fault.name = stratopi_gp0;
		gpioUsb2Disable.name = stratopi_gp31;
		gpioUsb2Fault.name = stratopi_gp1;
		gpioSoftSerTx.name = stratopi_gp37;
		gpioSoftSerRx.name = stratopi_gp33;
	} else {
		gpioBuzzer.name = stratopi_gp20;
		gpioWatchdogEnable.name = stratopi_gp6;
		gpioWatchdogHeartbeat.name = stratopi_gp5;
		gpioWatchdogExpired.gpio.name = stratopi_gp12;
		gpioShutdown.name = stratopi_gp16;
		gpioUpsBattery.gpio.name = stratopi_gp26;
		gpioRelay.name = stratopi_gp26;
		gpioSoftSerTx.name = stratopi_gp13;
		gpioSoftSerRx.name = stratopi_gp19;
	}
}

static bool softUartInit(void) {
	if (gpioInit(&gpioSoftSerTx)) {
		return false;
	}
	if (gpioInit(&gpioSoftSerRx)) {
		return false;
	}
	if (!raspberry_soft_uart_init(gpioSoftSerTx.desc, gpioSoftSerRx.desc)) {
		return false;
	}
	if (!raspberry_soft_uart_set_baudrate(1200)) {
		raspberry_soft_uart_finalize();
		return false;
	}
	msleep(50);
	return true;
}

static bool getFwVerAndModelNumber(int modelTry) {
	char *end = NULL;
	if (!softUartSendAndWait("XFW?", 4, 9, 300, false)
			&& softUartRxBuffIdx < 6) {
		return false;
	}
	fwVerMaj = simple_strtol((const char*) (softUartRxBuff + 3), &end, 10);
	fwVerMin = simple_strtol(end + 1, &end, 10);
	pr_info(LOG_TAG "FW version %d.%d\n", fwVerMaj, fwVerMin);
	if (fwVerMaj < 4) {
		if (modelTry == MODEL_CM && fwVerMin >= 5 && softUartRxBuffIdx == 6) {
			model_num = MODEL_CM;
			return true;
		}
		pr_err(LOG_TAG "FW version not supported\n");
		return false;
	}
	return (kstrtoint(end + 1, 10, &model_num) == 0);
}

static bool tryDetectFwVerAndModelAs(int modelTry) {
	model_num = modelTry;
	setGPIO();
	if (!softUartInit()) {
		return false;
	}
	if (!getFwVerAndModelNumber(modelTry)) {
		raspberry_soft_uart_finalize();
		return false;
	}
	return true;
}

static struct file* file_open(const char *path, int flags, int rights) {
	struct file *filp = NULL;
	int err = 0;
	filp = filp_open(path, flags, rights);
	if (IS_ERR(filp)) {
		err = PTR_ERR(filp);
		return NULL;
	}
	return filp;
}

static void file_close(struct file *file) {
	filp_close(file, NULL);
}

static int file_read(struct file *file, loff_t offset, unsigned char *buf,
		size_t count) {
	int ret;
	ret = kernel_read(file, buf, count, &offset);
	return ret;
}

static bool isComputeModule(void) {
	struct file *f = NULL;
	int res;
	unsigned char buff[29];
	f = file_open("/proc/device-tree/model", O_RDONLY, 0);
	if (!f) {
		pr_err(LOG_TAG "error opening file /proc/device-tree/model\n");
		return false;
	}
	res = file_read(f, 0, buff, 28);
	file_close(f);
	if (res < 28) {
		pr_err(LOG_TAG "error reading file /proc/device-tree/model\n");
		return false;
	}
	buff[28] = '\0';
	pr_info(LOG_TAG "RPi model: %s\n", buff);
	return (strstr(buff, "Compute Module") != NULL);
}

static bool detectFwVerAndModel(void) {
	if (isComputeModule()) {
		if (tryDetectFwVerAndModelAs(MODEL_CMDUO)) {
			return true;
		} else {
			return tryDetectFwVerAndModelAs(MODEL_CM);
		}
	} else {
		return tryDetectFwVerAndModelAs(MODEL_BASE);
	}
}

static int stratopi_init(struct platform_device *pdev) {
	int result = 0;
	bool modNumDetected = false;

	softUartInitialized = false;

	pr_info(LOG_TAG "init\n");

	mutex_init(&mcuMutex);

	gpioSetPlatformDev(pdev);

	if (!raspberry_soft_uart_set_rx_callback(&softUartRxCallback)) {
		pr_err(LOG_TAG "error setting soft UART callback\n");
		result = -1;
		goto fail;
	}

	if (model_num <= 0) {
		pr_info(LOG_TAG "detecting model...\n");
		modNumDetected = detectFwVerAndModel();
		if (!modNumDetected) {
			pr_err(LOG_TAG "error detecting model\n");
			if (model_num_fallback > 0) {
				pr_info(LOG_TAG "using fallback model number\n");
				model_num = model_num_fallback;
			} else {
				result = -1;
				goto fail;
			}
		}
	}

	if (!modNumDetected) {
		setGPIO();
		if (!softUartInit()) {
			pr_err(LOG_TAG "error initializing soft UART\n");
			result = -1;
			goto fail;
		}
	}

	pr_info(LOG_TAG "model=%d\n", model_num);

	if (model_num == MODEL_CM) {
		fwVerMaj = 3;
	}

	softUartInitialized = true;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,5,0)
	pDeviceClass = class_create("stratopi");
#else
	pDeviceClass = class_create(THIS_MODULE, "stratopi");
#endif
	if (IS_ERR(pDeviceClass)) {
		pr_err(LOG_TAG "failed to create device class\n");
		result = -1;
		goto fail;
	}

	if (model_num == MODEL_CM || model_num == MODEL_CMDUO
			|| model_num == MODEL_CM_2) {
		pLedDevice = device_create(pDeviceClass, NULL, 0, NULL, "led");
		pButtonDevice = device_create(pDeviceClass, NULL, 0, NULL, "button");

		if (IS_ERR(pLedDevice) || IS_ERR(pButtonDevice)) {
			pr_err(LOG_TAG "failed to create devices\n");
			result = -1;
			goto fail;
		}

		if (model_num == MODEL_CMDUO || model_num == MODEL_CM_2) {
			pExpBusDevice = device_create(pDeviceClass, NULL, 0, NULL,
					"expbus");
			pUsb1Device = device_create(pDeviceClass, NULL, 0, NULL, "usb1");
			pUsb2Device = device_create(pDeviceClass, NULL, 0, NULL, "usb2");

			if (IS_ERR(pExpBusDevice) || IS_ERR(pUsb1Device)
					|| IS_ERR(pUsb2Device)) {
				pr_err(LOG_TAG "failed to create devices\n");
				result = -1;
				goto fail;
			}
		}

		if (model_num == MODEL_CMDUO) {
			pSdDevice = device_create(pDeviceClass, NULL, 0, NULL, "sd");

			if (IS_ERR(pSdDevice)) {
				pr_err(LOG_TAG "failed to create devices\n");
				result = -1;
				goto fail;
			}
		}
	} else {
		pBuzzerDevice = device_create(pDeviceClass, NULL, 0, NULL, "buzzer");

		if (IS_ERR(pBuzzerDevice)) {
			pr_err(LOG_TAG "failed to create devices\n");
			result = -1;
			goto fail;
		}

		if (model_num == MODEL_CAN || model_num == MODEL_CAN_2) {
			pRelayDevice = device_create(pDeviceClass, NULL, 0, NULL, "relay");

			if (IS_ERR(pRelayDevice)) {
				pr_err(LOG_TAG "failed to create devices\n");
				result = -1;
				goto fail;
			}

		} else if (model_num == MODEL_UPS || model_num == MODEL_UPS_3) {
			pUpsDevice = device_create(pDeviceClass, NULL, 0, NULL, "ups");

			if (IS_ERR(pUpsDevice)) {
				pr_err(LOG_TAG "failed to create devices\n");
				result = -1;
				goto fail;
			}
		}
	}

	pWatchdogDevice = device_create(pDeviceClass, NULL, 0, NULL, "watchdog");
	pPowerDevice = device_create(pDeviceClass, NULL, 0, NULL, "power");
	pRs485Device = device_create(pDeviceClass, NULL, 0, NULL, "rs485");
	pMcuDevice = device_create(pDeviceClass, NULL, 0, NULL, "mcu");

	if (IS_ERR(pRs485Device) || IS_ERR(pWatchdogDevice) || IS_ERR(pPowerDevice)
			|| IS_ERR(pMcuDevice)) {
		pr_err(LOG_TAG "failed to create devices\n");
		result = -1;
		goto fail;
	}

	if (model_num >= MODEL_BASE_3) {
		pSecElDevice = device_create(pDeviceClass, NULL, 0, NULL, "sec_elem");

		if (IS_ERR(pSecElDevice)) {
			pr_err(LOG_TAG "failed to create devices\n");
			result = -1;
			goto fail;
		}
	}

	if (pBuzzerDevice) {
		result |= device_create_file(pBuzzerDevice, &devAttrBuzzerStatus);
		result |= device_create_file(pBuzzerDevice, &devAttrBuzzerBeep);
	}

	if (pWatchdogDevice) {
		result |= device_create_file(pWatchdogDevice, &devAttrWatchdogEnabled);
		result |= device_create_file(pWatchdogDevice,
				&devAttrWatchdogHeartbeat);
		result |= device_create_file(pWatchdogDevice, &devAttrWatchdogExpired);
		result |= device_create_file(pWatchdogDevice,
				&devAttrWatchdogEnableMode);
		result |= device_create_file(pWatchdogDevice, &devAttrWatchdogTimeout);
		result |= device_create_file(pWatchdogDevice,
				&devAttrWatchdogDownDelay);
		if (pSdDevice) {
			result |= device_create_file(pWatchdogDevice,
					&devAttrWatchdogSdSwitch);
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
		result |= device_create_file(pButtonDevice, &devAttrButtonStatusDeb);
		result |= device_create_file(pButtonDevice, &devAttrButtonStatusDebMs);
		result |= device_create_file(pButtonDevice, &devAttrButtonStatusDebCnt);
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
		if (model_num == MODEL_CMDUO || model_num == MODEL_UPS_3
				|| model_num == MODEL_BASE_3
				|| model_num == MODEL_CAN_2 || model_num == MODEL_CM_2) {
			result |= device_create_file(pMcuDevice, &devAttrMcuFwInstall);
			result |= device_create_file(pMcuDevice,
					&devAttrMcuFwInstallProgress);
		}
	}

	if (pSecElDevice) {
		result |= device_create_file(pSecElDevice, &devAttrSecElSerialNum);
	}

	if (result) {
		pr_err(LOG_TAG "failed to create device files\n");
		result = -1;
		goto fail;
	}

	if (pBuzzerDevice) {
		result |= gpioInit(&gpioBuzzer);
	}

	result |= gpioInit(&gpioWatchdogEnable);
	result |= gpioInit(&gpioWatchdogHeartbeat);
	result |= gpioInitDebounce(&gpioWatchdogExpired);

	result |= gpioInit(&gpioShutdown);

	if (pUpsDevice) {
		result |= gpioInitDebounce(&gpioUpsBattery);
	}

	if (pRelayDevice) {
		result |= gpioInit(&gpioRelay);
	}

	if (pLedDevice) {
		result |= gpioInit(&gpioLed);
	}

	if (pButtonDevice) {
		result |= gpioInitDebounce(&gpioButton);
	}

	if (pExpBusDevice) {
		result |= gpioInit(&gpioI2cExpEnable);
		result |= gpioInit(&gpioI2cExpFeedback);
	}

	if (pUsb1Device) {
		result |= gpioInit(&gpioUsb1Disable);
		result |= gpioInit(&gpioUsb1Fault);
	}

	if (pUsb2Device) {
		result |= gpioInit(&gpioUsb2Disable);
		result |= gpioInit(&gpioUsb2Fault);
	}

	if (result) {
		pr_err(LOG_TAG "error setting up GPIOs\n");
		goto fail;
	}

	pr_info(LOG_TAG "ready\n");
	return 0;

	fail:
	pr_err(LOG_TAG "init failed\n");
	cleanup();
	return result;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
static void stratopi_exit(struct platform_device *pdev) {
#else
static int stratopi_exit(struct platform_device *pdev) {
#endif
  cleanup();
  pr_info(LOG_TAG "exit\n");
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
  return 0;
#endif
}

const struct of_device_id stratopi_of_match[] = {
	{ .compatible = "sferalabs,stratopi", },
	{ },
};
MODULE_DEVICE_TABLE( of, stratopi_of_match);

static struct platform_driver stratopi_driver = {
	.probe = stratopi_init,
	.remove = stratopi_exit,
	.driver = {
		.name = "stratopi",
		.owner = THIS_MODULE,
		.of_match_table = stratopi_of_match,
	}
};

module_platform_driver(stratopi_driver);
