#ifndef _HD44780_H_
#define _HD44780_H_

#define BUF_SIZE	64

struct hd44780_geometry {
	int cols;
	int rows;
	int start_addrs[];
};

struct hd44780 {
	struct cdev cdev;
	struct device *device;
	struct i2c_client *i2c_client;
	struct hd44780_geometry *geometry;
	int addr;
	char buf[BUF_SIZE];
	struct mutex lock;
	struct list_head list;
};

// TODO: Add dynamic geometry selection via mod params, sysfs etc.
// TODO: Put known geometries into some kind of list/enum
static struct hd44780_geometry hd44780_geometry_20x4 = {
	.cols = 20,
	.rows = 4,
	.start_addrs = {0x00, 0x40, 0x14, 0x54},
};

static struct hd44780_geometry hd44780_geometry_16x2 = {
	.cols = 16,
	.rows = 2,
	.start_addrs = {0x00, 0x40},
};

static struct hd44780_geometry hd44780_geometry_8x1 = {
	.cols = 8,
	.rows = 1,
	.start_addrs = {0x00},
};

#endif
