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

#include "hd44780.h"

#define CLASS_NAME	"hd44780"
#define NAME		"hd44780"
#define NUM_DEVICES	8

void hd44780_write(struct hd44780 *, char *, size_t);
void hd44780_init_lcd(struct hd44780 *);
void hd44780_print(struct hd44780 *, char *);

static struct class *hd44780_class;
static dev_t dev_no;
/* We start with -1 so that first returned minor is 0 */
static atomic_t next_minor = ATOMIC_INIT(-1);



static LIST_HEAD(hd44780_list);
static DEFINE_SPINLOCK(hd44780_list_lock);

static int hd44780_file_open(struct inode *inode, struct file *filp)
{
	filp->private_data = container_of(inode->i_cdev, struct hd44780, cdev);
	return 0;
}

static int hd44780_file_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t hd44780_file_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp)
{
	struct hd44780 *lcd;
	size_t n;

	lcd = filp->private_data;
	n = min(count, (size_t)BUF_SIZE);

	// TODO: Consider using an interruptible lock
	mutex_lock(&lcd->lock);

	// TODO: Support partial writes during errors?
	if (copy_from_user(lcd->buf, buf, n))
		return -EFAULT;

	hd44780_write(lcd, lcd->buf, n);

	mutex_unlock(&lcd->lock);

	return n;
}

static void hd44780_init(struct hd44780 *lcd, struct hd44780_geometry *geometry,
		struct i2c_client *i2c_client)
{
	lcd->geometry = geometry;
	lcd->i2c_client = i2c_client;
	lcd->addr = 0x00;
	mutex_init(&lcd->lock);
}

static struct file_operations fops = {
	.open = hd44780_file_open,
	.release = hd44780_file_release,
	.write = hd44780_file_write,
};

static int hd44780_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	dev_t devt;
	struct hd44780 *lcd;
	struct device *device;
	int ret, minor;

	minor = atomic_inc_return(&next_minor);
	devt = MKDEV(MAJOR(dev_no), minor);

	lcd = (struct hd44780 *)kmalloc(sizeof(struct hd44780), GFP_KERNEL);
	if (!lcd) {
		return -ENOMEM;
	}

	hd44780_init(lcd, &hd44780_geometry_20x4, client);

	spin_lock(&hd44780_list_lock);
	list_add(&lcd->list, &hd44780_list);
	spin_unlock(&hd44780_list_lock);

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
	hd44780_print(lcd, "Hello, world!");
	
	return 0;

del_exit:
	cdev_del(&lcd->cdev);

	spin_lock(&hd44780_list_lock);
	list_del(&lcd->list);
	spin_unlock(&hd44780_list_lock);
exit:
	kfree(lcd);

	return ret;
}

static struct hd44780 * get_hd44780_by_i2c_client(struct i2c_client *i2c_client)
{
	struct hd44780 *lcd;

	spin_lock(&hd44780_list_lock);
	list_for_each_entry(lcd, &hd44780_list, list) {
		if (lcd->i2c_client->addr == i2c_client->addr) {
			spin_unlock(&hd44780_list_lock);
			return lcd;
		}
	}
	spin_unlock(&hd44780_list_lock);

	return NULL;
}


static int hd44780_remove(struct i2c_client *client)
{
	struct hd44780 *lcd;
	lcd = get_hd44780_by_i2c_client(client);
	device_destroy(hd44780_class, lcd->device->devt);
	cdev_del(&lcd->cdev);

	spin_lock(&hd44780_list_lock);
	list_del(&lcd->list);
	spin_unlock(&hd44780_list_lock);

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

static int __init hd44780_mod_init(void)
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
module_init(hd44780_mod_init);

static void __exit hd44780_mod_exit(void)
{
	i2c_del_driver(&hd44780_driver);
	class_destroy(hd44780_class);
	unregister_chrdev_region(dev_no, NUM_DEVICES);
}
module_exit(hd44780_mod_exit);

MODULE_AUTHOR("Mariusz Gorski <marius.gorski@gmail.com>");
MODULE_DESCRIPTION("HD44780 I2C via PCF8574 driver");
MODULE_LICENSE("GPL");

