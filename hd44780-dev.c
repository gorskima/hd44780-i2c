#include <linux/cdev.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>

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

struct hd44780_geometry *hd44780_geometries[] = {
	&hd44780_geometry_20x4,
	&hd44780_geometry_16x2,
	&hd44780_geometry_8x1,
	NULL
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
	
	/* Keep the RW bit low, because we write */
	data = data | (RW & 0x00);

	/* Flip the backlight bit */
	if (lcd->backlight)
		data |= BL;

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
static void hd44780_write_instruction_high_nibble(struct hd44780 *lcd, u8 data)
{
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

static void hd44780_write_data(struct hd44780 *lcd, u8 data)
{
	u8 h = (data >> 4) & 0x0F;
	u8 l = data & 0x0F;

	hd44780_write_nibble(lcd, DR, h);
	hd44780_write_nibble(lcd, DR, l);

	udelay(37 + 4);
}

static void hd44780_write_char(struct hd44780 *lcd, char ch)
{
	struct hd44780_geometry *geo = lcd->geometry;

	hd44780_write_data(lcd, ch);

	lcd->pos.col++;

	if (lcd->pos.col == geo->cols) {
		lcd->pos.row = (lcd->pos.row + 1) % geo->rows;
		lcd->pos.col = 0;
		hd44780_write_instruction(lcd, HD44780_DDRAM_ADDR | geo->start_addrs[lcd->pos.row]);
	}
}

static void hd44780_clear_display(struct hd44780 *lcd)
{
	hd44780_write_instruction(lcd, HD44780_CLEAR_DISPLAY);

	/* Wait for 1.64 ms because this one needs more time */
	udelay(1640);

	/*
	 * CLEAR_DISPLAY instruction also returns cursor to home,
	 * so we need to update it locally.
	 */
	lcd->pos.row = 0;
	lcd->pos.col = 0;
}

static void hd44780_clear_line(struct hd44780 *lcd)
{
	struct hd44780_geometry *geo;
	int start_addr, col;

	geo = lcd->geometry;
	start_addr = geo->start_addrs[lcd->pos.row];

	hd44780_write_instruction(lcd, HD44780_DDRAM_ADDR | start_addr);

	for (col = 0; col < geo->cols; col++)
		hd44780_write_data(lcd, ' ');

	hd44780_write_instruction(lcd, HD44780_DDRAM_ADDR | start_addr);
}

static void hd44780_handle_new_line(struct hd44780 *lcd)
{
	struct hd44780_geometry *geo = lcd->geometry;

	lcd->pos.row = (lcd->pos.row + 1) % geo->rows;
	lcd->pos.col = 0;
	hd44780_write_instruction(lcd, HD44780_DDRAM_ADDR
		| geo->start_addrs[lcd->pos.row]);
	hd44780_clear_line(lcd);
}

static void hd44780_handle_carriage_return(struct hd44780 *lcd)
{
	struct hd44780_geometry *geo = lcd->geometry;

	lcd->pos.col = 0;
	hd44780_write_instruction(lcd, HD44780_DDRAM_ADDR
		| geo->start_addrs[lcd->pos.row]);
}

static void hd44780_leave_esc_seq(struct hd44780 *lcd)
{
	memset(lcd->esc_seq_buf.buf, 0, ESC_SEQ_BUF_SIZE);
	lcd->esc_seq_buf.length = 0;
	lcd->is_in_esc_seq = false;
}

static void hd44780_flush_esc_seq(struct hd44780 *lcd)
{
	char *buf_to_flush;
	int buf_length;

	/* Copy and reset current esc seq */
	buf_to_flush = kmalloc(sizeof(char) * ESC_SEQ_BUF_SIZE, GFP_KERNEL);
	memcpy(buf_to_flush, lcd->esc_seq_buf.buf, ESC_SEQ_BUF_SIZE);
	buf_length = lcd->esc_seq_buf.length;

	hd44780_leave_esc_seq(lcd);

	/* Write \e that initiated current esc seq */
	hd44780_write_char(lcd, '\e');

	/* Flush current esc seq */
	hd44780_write(lcd, buf_to_flush, buf_length);

	kfree(buf_to_flush);
}

void hd44780_flush(struct hd44780 *lcd)
{
	while (lcd->is_in_esc_seq)
		hd44780_flush_esc_seq(lcd);
}

static void hd44780_handle_esc_seq_char(struct hd44780 *lcd, char ch)
{
	int prev_row, prev_col;

	lcd->esc_seq_buf.buf[lcd->esc_seq_buf.length++] = ch;

	if (!strcmp(lcd->esc_seq_buf.buf, "[2J")) {
		prev_row = lcd->pos.row;
		prev_col = lcd->pos.col;

		hd44780_clear_display(lcd);
		hd44780_write_instruction(lcd, HD44780_DDRAM_ADDR | (lcd->geometry->start_addrs[prev_row] + prev_col));

		hd44780_leave_esc_seq(lcd);
	} else if (!strcmp(lcd->esc_seq_buf.buf, "[H")) {
		hd44780_write_instruction(lcd, HD44780_RETURN_HOME);
		lcd->pos.row = 0;
		lcd->pos.col = 0;

		hd44780_leave_esc_seq(lcd);
	} else if (lcd->esc_seq_buf.length == ESC_SEQ_BUF_SIZE) {
		hd44780_flush_esc_seq(lcd);
	}
}

void hd44780_write(struct hd44780 *lcd, const char *buf, size_t count)
{
	size_t i;
	char ch;

	if (lcd->dirty) {
		hd44780_clear_display(lcd);
		lcd->dirty = false;
	}

	for (i = 0; i < count; i++) {
		ch = buf[i];

		if (lcd->is_in_esc_seq) {
			hd44780_handle_esc_seq_char(lcd, ch);
		} else {
			switch (ch) {
			case '\r':
				hd44780_handle_carriage_return(lcd);
				break;
			case '\n':
				hd44780_handle_new_line(lcd);
				break;
			case '\e':
				lcd->is_in_esc_seq = true;
				break;
			default:
				hd44780_write_char(lcd, ch);
				break;
			}
		}
	}
}

void hd44780_print(struct hd44780 *lcd, const char *str)
{
	hd44780_write(lcd, str, strlen(str));
}

void hd44780_set_geometry(struct hd44780 *lcd, struct hd44780_geometry *geo)
{
	lcd->geometry = geo;

	if (lcd->is_in_esc_seq);
		hd44780_leave_esc_seq(lcd);

	hd44780_clear_display(lcd);
}

void hd44780_set_backlight(struct hd44780 *lcd, bool backlight)
{
	lcd->backlight = backlight;
	pcf8574_raw_write(lcd, backlight ? BL : 0x00);
}

static void hd44780_update_display_ctrl(struct hd44780 *lcd)
{

	hd44780_write_instruction(lcd, HD44780_DISPLAY_CTRL
		| HD44780_D_DISPLAY_ON
		| (lcd->cursor_display ? HD44780_C_CURSOR_ON
			: HD44780_C_CURSOR_OFF)
		| (lcd->cursor_blink ? HD44780_B_BLINK_ON
			: HD44780_B_BLINK_OFF));
}

void hd44780_set_cursor_blink(struct hd44780 *lcd, bool cursor_blink)
{
	lcd->cursor_blink = cursor_blink;
	hd44780_update_display_ctrl(lcd);
}

void hd44780_set_cursor_display(struct hd44780 *lcd, bool cursor_display)
{
	lcd->cursor_display= cursor_display;
	hd44780_update_display_ctrl(lcd);
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

	hd44780_clear_display(lcd);

	hd44780_write_instruction(lcd, HD44780_ENTRY_MODE_SET
		| HD44780_ID_INCREMENT | HD44780_S_SHIFT_OFF);
}
