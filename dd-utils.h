/* SPDX-License-Identifier: GPL-2.0 */
/*
 * dd-utils.h
 *
 * Author: Clive Messer <clive.messer@digitaldreamtime.co.uk>
 *         Copyright (c) Digital Dreamtime Ltd 2020-2021
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __DD_UTILS_H
#define __DD_UTILS_H

#include <sound/soc.h>

#ifdef DDEBUG
char* dd_utils_log_daifmt_format(unsigned int format);
char* dd_utils_log_daifmt_clock(unsigned int format);
char* dd_utils_log_daifmt_inverse(unsigned int format);
char* dd_utils_log_daifmt_master(unsigned int format);
#endif /* DDEBUG */

#endif /* __DD_UTILS_H */
