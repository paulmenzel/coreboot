/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2012 secunet Security Networks AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "coreboot_tables.h"
#include "libpayload.h"
#include "x86/arch/io.h"
#include "cpuid.h"


// configure nuklear
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_IMPLEMENTATION
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_SOFTWARE_FONT
#include "cbui.h"
#define NK_RAWFB_IMPLEMENTATION
#include "nuklear/demo/x11_rawfb/nuklear_rawfb.h"

#ifdef NK_INCLUDE_DEFAULT_FONT
float get_textline_height(void)
{
	return 18.0f;
}

float get_textpad_height(void)
{
	return get_textline_height() + 5.0f;
}
#else
float get_textline_height(void)
{
#warning Implementing this depeding on choosen font !
	return 18;
}

float get_textpad_height(void)
{
#warning Implementing this depeding on choosen font !
	return get_textline_height() + 4.0f;
}
#endif

/* Function prototypes */
static void draw_buttons(void);
static void draw_modules(void);
static int save_modules(void);
static int update_modules(void);
static int init_modules(void);
static int handle_input(void);
static void shutdown(void);
static void store_reboot(void);
static void reboot(void);
static void halt_with_error(const char *fmt, ...);
static void do_events(void);

// configure cbui
extern struct cbui_module cpuinfo_module;
extern struct cbui_module pci_module;
extern struct cbui_module coreboot_module;
extern struct cbui_module nvram_module;
extern struct cbui_module bootlog_module;
extern struct cbui_module cbfs_module;
extern struct cbui_module timestamps_module;
extern struct cbui_module cmos_module;
extern struct cbui_module rtc_module;

struct cbui_module *system_modules[] = {
#if IS_ENABLED(CONFIG_MODULE_CPUINFO)
	&cpuinfo_module,
#endif
#if IS_ENABLED(CONFIG_MODULE_PCI)
	&pci_module,
#endif
#if IS_ENABLED(CONFIG_MODULE_NVRAM)
	&nvram_module,
#endif
#if IS_ENABLED(CONFIG_MODULE_RTC)
	&rtc_module,
#endif
};

struct cbui_module *firmware_modules[] = {
#if IS_ENABLED(CONFIG_MODULE_COREBOOT)
	&coreboot_module,
#endif
#if IS_ENABLED(CONFIG_MODULE_BOOTLOG)
	&bootlog_module,
#endif
#if IS_ENABLED(CONFIG_MODULE_CBFS)
	&cbfs_module,
#endif
#if IS_ENABLED(CONFIG_MODULE_TIMESTAMPS)
	&timestamps_module,
#endif
#if IS_ENABLED(CONFIG_MODULE_CMOS)
	&cmos_module,
#endif
};

static struct cbui_cat {
	char name[15];
	int cur;
	int count;
	struct cbui_module **modules;
} categories[] = {
	{
		.name = "System",
		.modules = system_modules,
		.count = ARRAY_SIZE(system_modules),
	},
	{
		.name = "Firmware",
		.modules = firmware_modules,
		.count = ARRAY_SIZE(firmware_modules),
	}
};

static struct cbui_buttons {
	char label[32];
	void (*func) (void);
} buttons[] = {
	{
		.label = "Save & Reboot",
		.func = reboot,
	},
	{
		.label = "Discard & Reboot",
		.func = store_reboot,
	},
	{
		.label = "Power off",
		.func = shutdown,
	}
};

static struct nk_context *ctx;
static struct cb_framebuffer *fbinfo;

/** Reboot */
static void reboot(void)
{
	outb(0x6, 0xcf9);
	halt();
}

/** Save data and reboot */
static void store_reboot(void)
{
	save_modules();
	outb(0x6, 0xcf9);
	halt();
}

/* Shutdown */
static void shutdown(void)
{
	/* FIXME */
	mouse_disconnect();
	keyboard_disconnect();
	halt();
}

static void halt_with_error(const char *fmt, ...)
{
	va_list ap;
	printf("FATAL ERROR: ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("System will halt now.\n");
	halt();
}

static float ceilf(float x)
{
	int a = (int)x;
	if ((float)a < x)
		a ++;
	return (float)a;
}

static inline void do_events(void)
{
	/* Poll usb here to support hot-plugin */
	usb_poll();
	/* Check ps/2 and USB HID for new data */
	mouse_poll();
}

/** Wait for VSYNC
 * Legacy VGA register VSYNC waiting
 */
static void vga_vsync(void)
{
	if (!IS_ENABLED(CONFIG_ARCH_X86))
		/* FIXME */
		return;

	do {
		do_events();
	} while (inb(0x3da) & 8);
}

/** Iterate over all module save functions
 * Allows modules to store data. Usually called before program exit.
 */
static int save_modules(void)
{
	int i, j, ret = 0;
	for (i = 0; i < ARRAY_SIZE(categories); i++) {
		for (j = 0; j < categories[i].count; j ++) {
			if (categories[i].modules[j]->save)
				ret |= categories[i].modules[j]->save();
			do_events();
		}
	}
	return ret;
}

/** Draw topmost action buttons
 * Draw buttons and show Function key associated with it.
 */
static void draw_buttons(void)
{
	int i;
	char buf[64];

	nk_layout_row_dynamic(ctx, get_textline_height(), ARRAY_SIZE(buttons));

	for (i = 0; i < ARRAY_SIZE(buttons); i++) {
		sprintf(buf, "%s [F%d]", buttons[i].label, i + 1);

		if (nk_button_label(ctx, buf))
			buttons[i].func();
	}
}

/** Iterate over all module's draw functions
 * Iterate over all module's draw functions and create a new
 * tab for each module.
 * Every category will have their own tree.
 * The tab's name is the module's name.
 */
static void draw_modules(void)
{
	int i, j, menu_idx;

	menu_idx = 0;
	for (i = 0; i < ARRAY_SIZE(categories); i++) {
		if (nk_tree_push_id(ctx, NK_TREE_TAB, categories[i].name, NK_MINIMIZED, menu_idx++)) {
			for (j = 0; j < categories[i].count; j ++) {
				if (nk_tree_push_id(ctx, NK_TREE_TAB,
					categories[i].modules[j]->name, NK_MINIMIZED, menu_idx++)) {
					if (categories[i].modules[j]->redraw)
						categories[i].modules[j]->redraw(ctx);
					do_events();

					nk_tree_pop(ctx);
				}
			}
			nk_tree_pop(ctx);
		}
	}
}

/** Iterate over all module update functions
 * Update module internal state and return UI redraw state.
 */
static int update_modules(void)
{
	int i, j, ret = 0;
	for (i = 0; i < ARRAY_SIZE(categories); i++) {
		for (j = 0; j < categories[i].count; j ++) {
			if (categories[i].modules[j]->update)
				ret |= categories[i].modules[j]->update();
			do_events();
		}
	}
	return ret;
}

/** Iterate over all module init functions
 * Initializes module internal state.
 */
static int init_modules(void)
{
	int i, j, ret = 0;
	int modules = 0, cnt = 0;

	for (i = 0; i < ARRAY_SIZE(categories); i++)
		modules += categories[i].count;

	for (i = 0; i < ARRAY_SIZE(categories); i++) {
		for (j = 0; j < categories[i].count; j ++) {
			if (categories[i].modules[j]->init)
				ret |= categories[i].modules[j]->init();

			/* No need to check division by zero */
			cbui_draw_splash_bar(fbinfo, 0x20 + ((cnt++ * 0xdf) / modules));
		}
	}

	return ret;
}

#if IS_ENABLED(CONFIG_SHOW_DATE_TIME)
/** Prints time in UTC to date.
 * Prints time in format HH:MM:SS to @time_str.
 * Returns 1 in case the time has changed since last call.
 */
static int print_time_and_date(char time_str[32])
{
	static struct timeval tv;
	struct timeval now;
	struct tm tm;

	gettimeofday(&now, NULL);
	if ((now.tv_sec != tv.tv_sec) && !nvram_updating()) {
		memcpy(&tv, &now, sizeof(tv));

		rtc_read_clock(&tm);

		sprintf(time_str, "Time: %02d/%02d/%04d - %02d:%02d:%02d",
			  tm.tm_mon + 1, tm.tm_mday, 1900 + tm.tm_year, tm.tm_hour,
			  tm.tm_min, tm.tm_sec);
		return 1;
	}

	return 0;
}
#endif



/** Handle mouse and keyboard events
 * Get and store mouse events.
 */
static int handle_input(void)
{
	static int cursor_x, cursor_y, cursor_z;
	static u32 cursor_buttons;
	int mx, my, mz;
	int ret = 0;
	u32 mb;

	mouse_get_rel(&mx, &my, &mz);
	mouse_get_buttons(&mb);

	if (mx | my | mz | ((mb ^ cursor_buttons) & 0x7))
		ret = 1;

	cursor_buttons = mb;

	cursor_x += mx;
	cursor_y += my;
	cursor_z = mz;

	if (cursor_x >= (int)fbinfo->x_resolution)
		cursor_x = fbinfo->x_resolution - 1;
	else if (cursor_x < 0)
		cursor_x = 0;
	if (cursor_y >= (int)fbinfo->y_resolution)
		cursor_y = fbinfo->y_resolution - 1;
	else if (cursor_y < 0)
		cursor_y = 0;

	if (ret) {
		nk_input_begin(ctx);

		nk_input_motion(ctx, cursor_x, cursor_y);
		//nk_input_scroll(ctx, cursor_z);
		nk_input_button(ctx, NK_BUTTON_LEFT, cursor_x, cursor_y, !!(cursor_buttons & 1));
		nk_input_button(ctx, NK_BUTTON_MIDDLE, cursor_x, cursor_y, !!(cursor_buttons & 2));
		nk_input_button(ctx, NK_BUTTON_RIGHT, cursor_x, cursor_y, !!(cursor_buttons & 4));

		/* FIXME: Add keyboard support */
		nk_input_end(ctx);
	}

	return ret;
}

int main(void)
{
	char tex_mem[512 * 512];
	void *back_buffer, *front_buffer;
	size_t framebuffer_size;
	int ret, once, main_v;
	const int border = 0;
	struct nk_rect rect;
	u32 events, eventsps;
	u32 fp, fps;

	struct timeval tv, now;
#if IS_ENABLED(CONFIG_SHOW_DATE_TIME)
	char time_str[32];
#endif

	/* Make sure that lib_sysinfo is initialized. */
	if (lib_get_sysinfo())
		halt_with_error("Could not retrieve sysinfo.\n");

	/* Get CPU features. */
	get_cpuid();

	/* CPU feature test. */
	if (!cpu_id.fid.bits.fpu)
		halt_with_error("FPU required, but not supported by CPU.\n");

	/* Make sure there is a framebuffer. */
	if (!lib_sysinfo.framebuffer)
		halt_with_error("Framebuffer is disabled. CBUI doesn't work in text mode.\n");

	fbinfo = (struct cb_framebuffer *)virt_to_phys(lib_sysinfo.framebuffer);
	if (!fbinfo->physical_address)
		halt_with_error("Framebuffer has no physical address.\n");

	cbui_draw_splash(fbinfo);
	cbui_draw_splash_bar(fbinfo, 0x08);

	/* Initialize framebuffer and backbuffer. */
	front_buffer = phys_to_virt(fbinfo->physical_address);
	framebuffer_size = fbinfo->x_resolution *
	    fbinfo->y_resolution *
	    (fbinfo->red_mask_size + fbinfo->green_mask_size + fbinfo->blue_mask_size);

	back_buffer = memalign(framebuffer_size, 32);
	if (!back_buffer)
		halt_with_error("Out of memory.\n");

	/* Init nuklear. */
	ctx = nk_rawfb_init(back_buffer,
			tex_mem,
			fbinfo->x_resolution,
			fbinfo->y_resolution,
			fbinfo->red_mask_size +fbinfo->green_mask_size +fbinfo->blue_mask_size);
	if (!ctx)
		halt_with_error("Failed to init nuklear.\n");

	cbui_draw_splash_bar(fbinfo, 0x10);
	/* Init usb driver. */
#if IS_ENABLED(CONFIG_LP_USB)
	usb_initialize();
	usb_poll();
#endif
	cbui_draw_splash_bar(fbinfo, 0x18);

	/* Init mouse driver. */
	cursor_init();

	cbui_draw_splash_bar(fbinfo, 0x20);

	/* Setup nuklear's window flags. */
	ctx->style.window.header.align = NK_HEADER_RIGHT;

	/* Enable nuklear's software cursor. */
	nk_style_show_cursor(ctx);

	/* Setup nuklear's root window dimensions. */
	rect = nk_rect(border, border, fbinfo->x_resolution - 2 * border, fbinfo->y_resolution - 2 * border);

	/* Call module init functions. */
	init_modules();

	/* Calculate main group vertical dimension */
	main_v = 4;
#if IS_ENABLED(CONFIG_SHOW_DATE_TIME)
	main_v ++;
#endif
	main_v = fbinfo->y_resolution - get_textline_height() * main_v;

	/* Init variables. */
	events = 0;
	eventsps = 0;
	fp = 0;
	fps = 0;
	gettimeofday(&tv, NULL);

	/* Causes the UI to be drawn once after entering the loop */
	once = 1;
	while (1) {
		do_events();

		ret = handle_input();
		ret |= update_modules();
#if IS_ENABLED(CONFIG_SHOW_DATE_TIME)
		ret |= print_time_and_date(time_str);
#endif
		/* Draw UI once right after entering the loop. */
		ret |= once;
		once = 0;

		/* Do once a second: */
		events++;
		gettimeofday(&now, NULL);
		if (now.tv_sec != tv.tv_sec) {
			tv.tv_sec = now.tv_sec;
			eventsps = events;
			fps = fp;
			fp = 0;
			events = 0;
			ret |= 1;
		}

		/* Only draw UI on input or module events. */
		if (ret) {
			if (nk_begin(ctx, "CoreBoot UserInterface " CONFIG_PAYLOAD_INFO_VERSION,
				    rect, NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {

				/* Describe nuklear UI */
				draw_buttons();

				/* Put tabs into invisible group box */
				nk_layout_row_dynamic(ctx, main_v, 1);
				if (nk_group_begin(ctx, "Main", 0)) {
					draw_modules();
					nk_group_end(ctx);
				}

				nk_layout_row_dynamic(ctx, get_textline_height(), 3);
#if IS_ENABLED(CONFIG_SHOW_DATE_TIME)
				nk_label(ctx, time_str, NK_TEXT_ALIGN_LEFT);
#endif
				nk_value_int(ctx, "Events/s", eventsps);
				nk_value_int(ctx, "Frames/s", fps);

				nk_end(ctx);
			}

			/* Process nuklear draw commands. */
			nk_rawfb_render(nk_rgb(30, 30, 30), 0);
			/* Wait until any previous retrace has ended. */
			vga_vsync();
			/* Blit buffers to prevent tearing. */
			memcpy_sse(front_buffer, back_buffer, framebuffer_size);
			/* Update frame counter. */
			fp ++;
		}
	}

	reboot();
}
