#include <linux/cdev.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#include "hd44780.h"

#define BL	0x08
#define E	0x04
#define RW	0x02
#define RS	0x01

#define HD44780_CLEAR_DISPLAY	0x01
#define HD44780_RETURN_HOME	0x02
#define HD44780_ENTRY_MODE_SET	0x04
#define HD44780_DISPLAY_CTRL	0x08
#define HD44780_SHIFT		0x10
#define HD44780_FUNCTION_SET	0x20
#define HD44780_CGRAM_ADDR	0x40
#define HD44780_DDRAM_ADDR	0x80

#define HD44780_DL_8BITS	0x10
#define HD44780_DL_4BITS	0x00
#define HD44780_N_2LINES	0x08
#define HD44780_N_1LINE		0x00

#define HD44780_D_DISPLAY_ON	0x04
#define HD44780_D_DISPLAY_OFF	0x00
#define HD44780_C_CURSOR_ON	0x02
#define HD44780_C_CURSOR_OFF	0x00
#define HD44780_B_BLINK_ON	0x01
#define HD44780_B_BLINK_OFF	0x00

#define HD44780_ID_INCREMENT	0x02
#define HD44780_ID_DECREMENT	0x00
#define HD44780_S_SHIFT_ON	0x01
#define HD44780_S_SHIFT_OFF	0x00

// TODO: Add dynamic geometry selection via mod params, sysfs etc.
// TODO: Put known geometries into some kind of list/enum
struct hd44780_geometry hd44780_geometry_20x4 = {
	.cols = 20,
	.rows = 4,
	.start_addrs = {0x00, 0x40, 0x14, 0x54},
};

struct hd44780_geometry hd44780_geometry_16x2 = {
	.cols = 16,
	.rows = 2,
	.start_addrs = {0x00, 0x40},
};

struct hd44780_geometry hd44780_geometry_8x1 = {
	.cols = 8,
	.rows = 1,
	.start_addrs = {0x00},
};

/* Defines possible register that we can write to */
typedef enum { IR, DR } dest_reg;

static void pcf8574_raw_write(struct hd44780 *lcd, u8 data)
{
	i2c_smbus_write_byte(lcd->i2c_client, data);
}

static void hd44780_write_nibble(struct hd44780 *lcd, dest_reg reg, u8 data)
{
	/* Shift the interesting data on the upper 4 bits (b7-b4) */
	data = (data << 4) & 0xF0;

	/* Flip the RS bit if we write do data register */
	if (reg == DR)
		data |= RS;
	
	/* Flip the backlight bit */
	data = data | (RW & 0x00) | BL;

	pcf8574_raw_write(lcd, data);
	/* Theoretically wait for tAS = 40ns, practically it's already elapsed */

	/* Raise the E signal... */
	pcf8574_raw_write(lcd, data | E);
	/* Again, "wait" for pwEH = 230ns */

	/* ...and let it fall to clock the data into the HD44780's register */
	pcf8574_raw_write(lcd, data);
	/* And again, "wait" for about tCYC_E - pwEH = 270ns */
}

/*
 * Takes a regular 8-bit instruction and writes it's high nibble into device's
 * instruction register. The low nibble is assumed to be all zeros. This is
 * used with a physical 4-bit bus when the device is still expecting 8-bit
 * instructions.
 */
static void hd44780_write_instruction_high_nibble(struct hd44780 *lcd, u8 data) {
	u8 h = (data >> 4) & 0x0F;

	hd44780_write_nibble(lcd, IR, h);
	
	udelay(37);
}

static void hd44780_write_instruction(struct hd44780 *lcd, u8 data)
{
	u8 h = (data >> 4) & 0x0F;
	u8 l = data & 0x0F;

	hd44780_write_nibble(lcd, IR, h);
	hd44780_write_nibble(lcd, IR, l);

	udelay(37);
}

static int reached_end_of_line(struct hd44780_geometry *geo, int row, int addr)
{
	return addr == geo->start_addrs[row] + geo->cols;
}

static void hd44780_write_data(struct hd44780 *lcd, u8 data)
{
	u8 h = (data >> 4) & 0x0F;
	u8 l = data & 0x0F;

	hd44780_write_nibble(lcd, DR, h);
	hd44780_write_nibble(lcd, DR, l);

	udelay(37 + 4);

	lcd->addr++;
}

static void hd44780_write_char(struct hd44780 *lcd, char ch)
{
	int row;
	struct hd44780_geometry *geo = lcd->geometry;

	hd44780_write_data(lcd, ch);

	for (row = 0; row < geo->rows; row++) {
		if (reached_end_of_line(geo, row, lcd->addr)) {
			lcd->addr = geo->start_addrs[row + 1 % geo->rows];
			hd44780_write_instruction(lcd, HD44780_DDRAM_ADDR | lcd->addr);
			break;
		}
	}
}

static void hd44780_clear_line(struct hd44780 *lcd)
{
	struct hd44780_geometry *geo;
	int row, col, start_addr;

	geo = lcd->geometry;

	for (row = 0; row < geo->rows; row++) {
		if (lcd->addr >= geo->start_addrs[row]
				&& lcd->addr < geo->start_addrs[row]
					+ geo->cols) {

			start_addr = geo->start_addrs[row];

			hd44780_write_instruction(lcd, HD44780_DDRAM_ADDR
				| start_addr);

			for (col = 0; col < geo->cols; col++)
				hd44780_write_data(lcd, ' ');

			lcd->addr = start_addr;
			hd44780_write_instruction(lcd, HD44780_DDRAM_ADDR
				| start_addr);
			break;
		}
	}
}

static void hd44780_handle_new_line(struct hd44780 *lcd)
{
	struct hd44780_geometry *geo;
	int row, new_row;

	geo = lcd->geometry;

	for (row = 0; row < geo->rows; row++) {
		if (lcd->addr >= geo->start_addrs[row]
				&& lcd->addr < geo->start_addrs[row]
					+ geo->cols) {

			new_row = (row + 1) % geo->rows;
			lcd->addr = geo->start_addrs[new_row];
			hd44780_write_instruction(lcd, HD44780_DDRAM_ADDR
				| lcd->addr);
			hd44780_clear_line(lcd);
			break;
		}
	}
}

void hd44780_write(struct hd44780 *lcd, char *buf, size_t count)
{
	size_t i;
	char ch;

	for (i = 0; i < count; i++) {
		ch = buf[i];

		switch (ch) {
		case '\n':
			hd44780_handle_new_line(lcd);
			break;
		default:
			hd44780_write_char(lcd, ch);
			break;
		}
	}
}

void hd44780_print(struct hd44780 *lcd, char *str)
{
	hd44780_write(lcd, str, strlen(str));
}

void hd44780_init_lcd(struct hd44780 *lcd)
{
	hd44780_write_instruction_high_nibble(lcd, HD44780_FUNCTION_SET
		| HD44780_DL_8BITS);
	mdelay(5);

	hd44780_write_instruction_high_nibble(lcd, HD44780_FUNCTION_SET
		| HD44780_DL_8BITS);
	udelay(100);

	hd44780_write_instruction_high_nibble(lcd, HD44780_FUNCTION_SET
		| HD44780_DL_8BITS);
	
	hd44780_write_instruction_high_nibble(lcd, HD44780_FUNCTION_SET
		| HD44780_DL_4BITS);

	hd44780_write_instruction(lcd, HD44780_FUNCTION_SET | HD44780_DL_4BITS
		| HD44780_N_2LINES);

	hd44780_write_instruction(lcd, HD44780_DISPLAY_CTRL | HD44780_D_DISPLAY_ON
		| HD44780_C_CURSOR_ON | HD44780_B_BLINK_ON);

	hd44780_write_instruction(lcd, HD44780_CLEAR_DISPLAY);
	/* Wait for 1.64 ms because this one needs more time */
	udelay(1640);

	hd44780_write_instruction(lcd, HD44780_ENTRY_MODE_SET
		| HD44780_ID_INCREMENT | HD44780_S_SHIFT_OFF);
}
