/*
 * This file is part of the coreinfo project.
 *
 * Copyright (C) 2008 Advanced Micro Devices, Inc.
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

#ifndef COREINFO_H_
#define COREINFO_H_

#include <libpayload.h>
#include <config.h>

#ifndef NDEBUG
#define NK_ASSERT(statement)						\
	do {											\
		if ((statement) == 0) {						\
			printf("assertion %s failed in file %s, "	\
			    "function %s(), line %d\n",				\
				#statement, __FILE__, __FUNCTION__, __LINE__);	\
			halt();										\
		}											\
	} while(0)
#else
#define NK_ASSERT(x) do {} while(0)
#endif

#ifndef assert
#define assert(x) do {} while(0);
#endif

static float ceilf(float x);

#include "../cbui/nuklear/nuklear.h"

struct cbui_module {
	char name[15];
	/** Initialize the module.
	 * Returns 0 on success. The module is disabled on error.
	 * No further methods will be called, except for terminate.
	 * Do computional intense task here.
	 */
	int (*init) (void);
	/** Periodical called update method.
	 * Periodical called by the main loop.
	 * Any value other than 0 causes the UI to be refreshed.
	 */
	int (*update) (void);
	/** Draw method. Should be as fast as possible.
	 * Called by the main loop in case the UI needs to be refreshed.
	 * Returns 0 on success.
	 */
	int (*redraw) (struct nk_context *);
	/** Handle keyboard events.
	 * TODO
	 * Returns 0 on success.
	 */
	int (*handle) (int);
	/** Save internal state.
	 * Is called in case the user want's to save data.
	 * Returns 0 on success.
	 */
	int (*save) (void);
	/** Terminate method.
	 * Is called last right before the programm is terminated.
	 * Returns 0 on success.
	 */
	void (*terminate) (void);
};

/* splash.c */
void cbui_draw_splash(struct cb_framebuffer *fbinfo);
void cbui_draw_splash_bar(struct cb_framebuffer *fbinfo, u8 perc);

/* cbui.c */
float get_textline_height(void);
float get_textpad_height(void);

/* memcpy.c */
void *memcpy_sse(void *dest, const void *src, size_t len);

#endif
