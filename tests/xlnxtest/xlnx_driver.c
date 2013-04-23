/*
 * Copyright (C) 2013 Xilinx, Inc. All rights reserved.
 *
 * From the Xilinx Zynq 14.4 TRD application
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

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

///////////////// XVDMA Support //////////

#define VDMA_ID_TPG		0		// vdma id as per the dts file
#define VDMA_ID_SOBEL	1

//IOCTL(s)
#define XVDMA_IOCTL_BASE	'W'
/* XVDMA_GET_NUM_DEVICES:
-- Obtains the number of VDMA probed and available for use.
-- Argument: Address of unsigned int [unsigned int *].
	- This gets filled up with the number of VDMA when the call returns.
*/
#define XVDMA_GET_NUM_DEVICES	_IO(XVDMA_IOCTL_BASE, 0)

/* XVDMA_GET_DEV_INFO:
-- Gives device information like channel number, for the given VDMA ID.
-- Argument: Address of struct xvdma_dev [struct xvdma_dev *].
	- Before calling, the device_id field of this structure should be filled with VDMA
	ID. On return the rest of the structure is filled by the driver.
*/
#define XVDMA_GET_DEV_INFO		_IO(XVDMA_IOCTL_BASE, 1)

/* XVDMA_DEVICE_CONTROL:
-- Sets the VDMA channel configuration.
-- Argument: Address of struct xvdma_chan_cfg [struct xvdma_chan_cfg *].
	- Before calling, this structure should be filled with required channel
	configurations.
	- To reset VDMA, only fill chan = <channel id> and config.reset = 1
	fields of structure.
*/
#define XVDMA_DEVICE_CONTROL	_IO(XVDMA_IOCTL_BASE, 2)

/* XVDMA_PREP_BUF:
-- Sets the buffer configurations.
-- Argument: Address of struct xvdma_buf_info [struct xvdma_buf_info *].
	- Before calling, this structure should be filled with required buffer configurations.
*/
#define XVDMA_PREP_BUF			_IO(XVDMA_IOCTL_BASE, 3)

/* XVDMA_START_TRANSFER:
-- Triggers the VDMA transfer.
-- Argument: Address of struct xvdma_transfer [struct xvdma_transfer *].
	- Before calling, this structure should be filled. The structure specifies the channel ID
	and whether the call is synchronous or asynchronous.
*/
#define XVDMA_START_TRANSFER	_IO(XVDMA_IOCTL_BASE, 4)

/* XVDMA_STOP_TRANSFER:
-- This call stops the VDMA.
-- Argument: Address of the unsigned int variable [unsigned int *].
	- Before calling, this int variable should be filled with the channel ID.
*/
#define XVDMA_STOP_TRANSFER		_IO(XVDMA_IOCTL_BASE, 5)

/*
 * enum to define the direction of the dma channel
 */

 enum dma_transfer_direction {
	DMA_MEM_TO_MEM = 0,
	DMA_MEM_TO_DEV = 1,
	DMA_DEV_TO_MEM = 2,
	DMA_DEV_TO_DEV = 3,
	DMA_TRANS_NONE,
};

/* DMA Device configuration structure
 *
 * Xilinx CDMA and Xilinx DMA only use interrupt coalescing and delay counter
 * settings.
 *
 * If used to start/stop parking mode for Xilinx VDMA, vsize must be -1
 * If used to set interrupt coalescing and delay counter only for
 * Xilinx VDMA, hsize must be -1 */
struct xilinx_dma_config {
	enum dma_transfer_direction direction; /* Channel direction */
	int vsize;                         /* Vertical size */
	int hsize;                         /* Horizontal size */
	int stride;                        /* Stride */
	int frm_dly;                       /* Frame delay */
	int gen_lock;                      /* Whether in gen-lock mode */
	int master;                        /* Master that it syncs to */
	int frm_cnt_en;                    /* Enable frame count enable */
	int park;                          /* Whether wants to park */
	int park_frm;                      /* Frame to park on */
	int coalesc;                       /* Interrupt coalescing threshold */
	int delay;                         /* Delay counter */
	int disable_intr;                  /* Whether use interrupts */
	int reset;			   /* Reset Channel */
	int ext_fsync;			   /* External Frame Sync */
};

/* structure to get the channel info for the given vdma
 *
 */
struct xvdma_dev
{
        unsigned int tx_chan;			/* tx channel id, (address of the channel) */
        unsigned int rx_chan;			/* tx channel id, (address of the channel) */
        unsigned int device_id;			/* device id as per the decive tree*/
};
/*
 * structure describing the dma configuration.
 */
struct xvdma_chan_cfg {
        struct xilinx_dma_config config;
        unsigned int chan;
};

/*
 * structure describing the buffer properties
 */
struct xvdma_buf_info {
	unsigned int chan;				/* channel id returned in xvdma_dev through get info */
	unsigned int device_id;
	unsigned int direction;			/* direction of the channel, with which this buffer will be associated */
	unsigned int shared_buffer;		/* Whether the given buffer will be shared with other vdma */
	unsigned int mem_type;			/* memory type is read / write -- value will be same as direction */
	unsigned int fixed_buffer;		/* Fixed buffer indicate that the user is responsible for the buffer allocation, else driver does it*/
	unsigned int buf_size;			/* Size of each buffer */
	unsigned int addr_base;			/* Base address of the first buffer */
	unsigned int frm_cnt;			/* total number of buffers to be configure */
	unsigned int callback;			/* This is not a callback fn; its just indication for driver to register its internal callback or not */
};

struct xvdma_transfer {
	unsigned int chan;		/* channel id */
	unsigned int wait;		/* if 1, then the start call is synchronous, if 0, then teh start call is asynchronous */
};

static const unsigned int gStrideLength = 1920;

enum VideoParameters{
	E_HActive,						// Width / Horizontal Active video
	E_VActive,						// Height / Vertical Active video
	E_VParam_MAX
};

#define TPG_BASE	0x40080000

// TPG parameter -updated as per new TPG core.
#define TPG_PATTERN					0x0100
#define TPG_MOTION					0x0104

#define TPG_FRM_SIZE				0x0020
#define TPG_ZPLATE_H				0x010C
#define TPG_ZPLATE_V				0x0110
#define TPG_BOX_SIZE				0x0114
#define TPG_BOX_COLOUR				0x118
#define TPG_CONTROL                 0x0000

#define REG_WRITE(addr, off, val) (*(volatile int*)(addr+off)=(val))

void set_xlnx_tpg(uint32_t width, uint32_t height, int tpg_modifier)
{
	int fd = open("/dev/mem", O_RDWR);
	void *tpg_base_address = mmap(NULL, 0x20, PROT_READ | PROT_WRITE, MAP_SHARED, fd, TPG_BASE);
	if (tpg_base_address == MAP_FAILED) {
		fprintf(stderr, "failed map tpg base \n");
		return;
	}

	/* Pattern set */
   REG_WRITE(tpg_base_address, TPG_PATTERN, 0x000010EA);
	//Motion
   REG_WRITE(tpg_base_address, TPG_MOTION, 0x0000000B);
   REG_WRITE(tpg_base_address, TPG_FRM_SIZE, (height << 16) | width);			// specific to 1080p
   REG_WRITE(tpg_base_address, TPG_ZPLATE_H, (0x4A * 1920) / width);
   REG_WRITE(tpg_base_address, TPG_ZPLATE_V, (0x3 * 1080) / height);
   REG_WRITE(tpg_base_address, TPG_BOX_SIZE, (0x70 * height) / 1080);
   REG_WRITE(tpg_base_address, TPG_BOX_COLOUR, 0x76543210 & (~0xFF << (tpg_modifier % 4 * 8)));

	/* Starting TPG pattern */
   REG_WRITE(tpg_base_address, TPG_CONTROL, 0x00000003);

	munmap(tpg_base_address, 0x20);
	close(fd);
}

void reset_xlnx_vdma(void)
{
	int fd_vdma;
	struct xvdma_chan_cfg chan_cfg;
	struct xvdma_dev xdma_device_info;

	if ((fd_vdma = open("/dev/xvdma", O_RDWR)) < 0) {
		printf("Cannot open device node xvdma\n");
	}

	xdma_device_info.device_id = 0;
	if (ioctl(fd_vdma, XVDMA_GET_DEV_INFO, &xdma_device_info) < 0) {
		printf("%s: Failed to get info for device id:%d", __func__, 0);
	}

	chan_cfg.chan = xdma_device_info.rx_chan;

	chan_cfg.config.reset = 1;
	chan_cfg.config.direction = DMA_DEV_TO_MEM;

	if (ioctl(fd_vdma, XVDMA_STOP_TRANSFER, &(chan_cfg.chan)) < 0)
		printf("VDMA: XVDMA_STOP_TRANSFER calling failed\n");


	if (ioctl(fd_vdma, XVDMA_DEVICE_CONTROL, &chan_cfg) < 0)
		printf("VDMA: XVDMA_DEVICE_CONTROL calling failed\n");

	close(fd_vdma);

}

void configure_and_start_xlnx_vdma(uint32_t width, uint32_t height, void *addr)
{
	struct xvdma_dev xvdma_dev;
	struct xvdma_chan_cfg chan_cfg;
	struct xvdma_buf_info buf_info;
	struct xvdma_dev xdma_device_info;
	struct xvdma_transfer xfer_param;
	int fd_vdma;

	if ((fd_vdma = open("/dev/xvdma", O_RDWR)) < 0) {
		printf("Cannot open device node xvdma\n");
	}

	xvdma_dev.device_id = 0;

	if (ioctl(fd_vdma, XVDMA_GET_DEV_INFO, &xvdma_dev) < 0) {
		printf("%s: Failed to get info for device id:%d", __func__, 0);
	}

	chan_cfg.chan = xvdma_dev.rx_chan;
	buf_info.chan = xvdma_dev.rx_chan;
	buf_info.mem_type = DMA_DEV_TO_MEM;

	/* Set up hardware configuration information */

	chan_cfg.config.vsize = height;
	chan_cfg.config.hsize = width* 4; // width length in bytes (Assuming always a pixel is of 32bpp / 24bpp unpacked)
	chan_cfg.config.stride = width * 4;		// stride length in bytes (Assuming always a pixel is of 32bpp / 24bpp unpacked)

	chan_cfg.config.frm_cnt_en = 0; /* irq interrupt disabled(0), enabled(1) */
	chan_cfg.config.frm_dly = 0;
	chan_cfg.config.park = 0; /* circular_buf_en(0), park_mode(1) */
	chan_cfg.config.gen_lock = 0; /* Gen-Lock */
	chan_cfg.config.disable_intr = 0;
	chan_cfg.config.direction = DMA_DEV_TO_MEM;
	chan_cfg.config.reset = 0;
	chan_cfg.config.coalesc = 0;
	chan_cfg.config.delay = 0;
	chan_cfg.config.master = 0;
	chan_cfg.config.ext_fsync = 2; //fsync type

	if (ioctl(fd_vdma, XVDMA_DEVICE_CONTROL, &chan_cfg) < 0)			// --1--
		printf("VDMA: XVDMA_DEVICE_CONTROL calling failed\n");

	buf_info.device_id = 0;
	buf_info.direction = DMA_DEV_TO_MEM;
	buf_info.shared_buffer = 0;
	buf_info.fixed_buffer = 1;
	buf_info.addr_base = (unsigned int)addr;
	buf_info.buf_size = 0x00870000;
	buf_info.frm_cnt = 1;
	if (ioctl(fd_vdma, XVDMA_PREP_BUF, &buf_info) < 0)
		printf("%s: Calling XVDMA_PREP_BUF failed\n",__func__);

	xdma_device_info.device_id = 0;
	if (ioctl(fd_vdma, XVDMA_GET_DEV_INFO, &xdma_device_info) < 0) {
		printf("%s: Failed to get info for device id:%d", __func__, 0);
	}

	chan_cfg.chan = xdma_device_info.rx_chan;

	xfer_param.chan =  xdma_device_info.rx_chan;
	xfer_param.wait = 0;

	if (ioctl(fd_vdma, XVDMA_START_TRANSFER, &xfer_param) < 0)
		printf("%s: Calling XVDMA_START_TRANSFER failed\n",__func__);

	close(fd_vdma);
}
