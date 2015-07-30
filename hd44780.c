#include <linux/module.h>
#include <linux/fs.h>
#include <linux/printk.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/list.h>

#define BL	0x08
#define E	0x04
#define RW	0x02
#define RS	0x01

#define CLASS_NAME	"hd44780"
#define NAME		"hd44780"
#define NUM_DEVICES	8

static struct class *hd44780_class;
static dev_t dev_no;
static int next_minor = 0;

struct hd44780 {
	struct cdev cdev;
	struct device *device;
	struct i2c_client *i2c_client;
	struct list_head list;
};

static LIST_HEAD(hd44780_list);

static void hd44780_raw_write(struct hd44780 *lcd, int data)
{
	i2c_smbus_write_byte(lcd->i2c_client, data);
}

static void write4(struct hd44780 *lcd, int data) {
	int l = data & 0x0F;
	int cmd = (l << 4) | (RS & 0x00) | (RW & 0x00) | BL;
	hd44780_raw_write(lcd, cmd);
	msleep(10);
	hd44780_raw_write(lcd, cmd | E);
	msleep(10);
	hd44780_raw_write(lcd, cmd);
	msleep(10);

}

static void hd44780_command(struct hd44780 *lcd, int data)
{
	int h = (data >> 4) & 0x0F;
	int l = data & 0x0F;
	int cmd_h, cmd_l;

	cmd_h = (h << 4) | (RS & 0x00) | (RW & 0x00) | BL;
	hd44780_raw_write(lcd, cmd_h);
	msleep(10);
	hd44780_raw_write(lcd, cmd_h | E);
	msleep(10);
	hd44780_raw_write(lcd, cmd_h);
	msleep(10);

	cmd_l = (l << 4) | (RS & 0x00) | (RW & 0x00) | BL;
        hd44780_raw_write(lcd, cmd_l);
        msleep(10);
        hd44780_raw_write(lcd, cmd_l | E);
        msleep(10);
        hd44780_raw_write(lcd, cmd_l);
        msleep(10);
}

static void hd44780_data(struct hd44780 *lcd, int data)
{
	int h = (data >> 4) & 0x0F;
	int l = data & 0x0F;
	int cmd_h, cmd_l;

	cmd_h = (h << 4) | RS | (RW & 0x00) | BL;
	hd44780_raw_write(lcd, cmd_h);
	msleep(10);
	hd44780_raw_write(lcd, cmd_h | E);
	msleep(10);
	hd44780_raw_write(lcd, cmd_h);
	msleep(10);

	cmd_l = (l << 4) | RS | (RW & 0x00) | BL;
        hd44780_raw_write(lcd, cmd_l);
        msleep(10);
        hd44780_raw_write(lcd, cmd_l | E);
        msleep(10);
        hd44780_raw_write(lcd, cmd_l);
        msleep(10);
}

static void hd44780_init_lcd(struct hd44780 *lcd)
{
	write4(lcd, 0x03);
	// wait 4.1 ms
	write4(lcd, 0x03);
	// wait 100 us
	write4(lcd, 0x03);
	
	// init 4bit commands
	write4(lcd, 0x02);

	// function set: 4bit, 1 line, 5x8 dots
	hd44780_command(lcd, 0x20);

	// display on, cursor on, blink on
	hd44780_command(lcd, 0x0F);

	// clear screen
	hd44780_command(lcd, 0x01);
}

static void hd44780_write_str(struct hd44780 *lcd, char *str)
{
	while (*str != 0)
		hd44780_data(lcd, *str++);
}

static int hd44780_open(struct inode *inode, struct file *filp)
{
	filp->private_data = container_of(inode->i_cdev, struct hd44780, cdev);
	return 0;
}

static int hd44780_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t hd44780_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp)
{
	char b;
	copy_from_user(&b, buf, 1);
	hd44780_data(filp->private_data, b);
	return 1;
}

static struct file_operations fops = {
	.open = hd44780_open,
	.write = hd44780_write,
	.release = hd44780_release
};

static int hd44780_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	dev_t devt;
	struct hd44780 *lcd;
	struct device *device;
	int ret;

	devt = MKDEV(MAJOR(dev_no), next_minor++);

	lcd = (struct hd44780 *)kmalloc(sizeof(struct hd44780), GFP_KERNEL);
	if (!lcd) {
		return -ENOMEM;
	}

	lcd->i2c_client = client;

	list_add(&lcd->list, &hd44780_list);

	cdev_init(&lcd->cdev, &fops);
	ret = cdev_add(&lcd->cdev, devt, 1);
	if (ret) {
		pr_warn("Can't add cdev\n");
		goto exit;
	}
	
	device = device_create(hd44780_class, NULL, devt, NULL, "lcd%d", MINOR(devt));
	if (IS_ERR(device)) {
		ret = PTR_ERR(device);
		pr_warn("Can't create device\n");
		goto del_exit;
	}
	lcd->device = device;

	hd44780_init_lcd(lcd);
	hd44780_write_str(lcd, "Hello, world!");
	
	return 0;

del_exit:
	cdev_del(&lcd->cdev);
	list_del(&lcd->list);
exit:
	kfree(lcd);

	return ret;
}

static struct hd44780 * get_hd44780_by_i2c_client(struct i2c_client *i2c_client)
{
	struct hd44780 *lcd;
	list_for_each_entry(lcd, &hd44780_list, list) {
		if (lcd->i2c_client->addr == i2c_client->addr) {
			return lcd;
		}
	}
        return NULL;
}


static int hd44780_remove(struct i2c_client *client)
{
	struct hd44780 *lcd;
	lcd = get_hd44780_by_i2c_client(client);
	device_destroy(hd44780_class, lcd->device->devt);
	cdev_del(&lcd->cdev);
	list_del(&lcd->list);
	kfree(lcd);
	
	return 0;
}

static const struct i2c_device_id hd44780_id[] = {
	{ NAME, 0},
	{ }
};

static struct i2c_driver hd44780_driver = {
	.driver = {
		.name	= NAME,
		.owner	= THIS_MODULE,
	},
	.probe = hd44780_probe,
	.remove = hd44780_remove,
	.id_table = hd44780_id,
};

static int __init hd44780_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&dev_no, 0, NUM_DEVICES, NAME);
	if (ret) {
		pr_warn("Can't allocate chardev region");
		return ret;
	}

	hd44780_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(hd44780_class)) {
		ret = PTR_ERR(hd44780_class);
		pr_warn("Can't create %s class\n", CLASS_NAME);
		goto exit;
	}

	ret = i2c_add_driver(&hd44780_driver);
	if (ret) {
		pr_warn("Can't register I2C driver %s\n", hd44780_driver.driver.name);
		goto destroy_exit;
	}

	return 0;

destroy_exit:
	class_destroy(hd44780_class);
exit:
	unregister_chrdev_region(dev_no, NUM_DEVICES);

	return ret;
}
module_init(hd44780_init);

static void __exit hd44780_exit(void)
{
	i2c_del_driver(&hd44780_driver);
	class_destroy(hd44780_class);
	unregister_chrdev_region(dev_no, NUM_DEVICES);
}
module_exit(hd44780_exit);

MODULE_AUTHOR("Mariusz Gorski <marius.gorski@gmail.com>");
MODULE_DESCRIPTION("HD44780 I2C via PCF8574 driver");
MODULE_LICENSE("GPL");

