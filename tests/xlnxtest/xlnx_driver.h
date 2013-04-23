/*
 * Copyright (C) 2013 Xilinx, Inc. All rights reserved.
 *
 * Authors:
 *	hyun woo kwon <hyunk@xilinx.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef XLNX_DRIVER_H_
#define XLNX_DRIVER_H_

void set_xlnx_tpg(uint32_t width, uint32_t height, int tpg_modifier);
void reset_xlnx_vdma(void);
void configure_and_start_xlnx_vdma(uint32_t width, uint32_t height, void *addr);

#endif /* XLNX_DRIVER_H_ */
