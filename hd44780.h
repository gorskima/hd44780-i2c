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

void hd44780_write(struct hd44780 *, char *, size_t);
void hd44780_init_lcd(struct hd44780 *);
void hd44780_print(struct hd44780 *, char *);

extern struct hd44780_geometry hd44780_geometry_20x4;

#endif
