/*
 * SerialICE
 *
 * Copyright (C) 2009 coresystems GmbH
 * Copyright (C) 2016 Patrick Rudolph <siro@das-labor.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef CONSOLE_SERIALICE_H
#define CONSOLE_SERIALICE_H

#if (ENV_RAMSTAGE && CONFIG_DEBUG_SERIALICE_RAMSTAGE) || \
	(ENV_ROMSTAGE && CONFIG_DEBUG_SERIALICE_ROMSTAGE)
void serialice_main(void);
#else
static inline void serialice_main(void) { }
#endif

#endif
