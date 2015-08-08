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

extern struct hd44780_geometry hd44780_geometry_20x4;

#endif
