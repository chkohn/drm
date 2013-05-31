/*
 * Copyright (C) 2013 Xilinx, Inc. All rights reserved.
 *
 * Based on modetest
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

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/poll.h>
#include <sys/time.h>

#include "xf86drm.h"
#include "xf86drmMode.h"
#include "drm_fourcc.h"
#include "libkms.h"

#include "buffers.h"

#include "xlnx_driver.h"

drmModeRes *resources;
int fd, modes;

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct type_name {
	int type;
	char *name;
};

#define type_name_fn(res) \
char * res##_str(int type) {			\
	unsigned int i;					\
	for (i = 0; i < ARRAY_SIZE(res##_names); i++) { \
		if (res##_names[i].type == type)	\
			return res##_names[i].name;	\
	}						\
	return "(invalid)";				\
}

struct type_name encoder_type_names[] = {
	{ DRM_MODE_ENCODER_NONE, "none" },
	{ DRM_MODE_ENCODER_DAC, "DAC" },
	{ DRM_MODE_ENCODER_TMDS, "TMDS" },
	{ DRM_MODE_ENCODER_LVDS, "LVDS" },
	{ DRM_MODE_ENCODER_TVDAC, "TVDAC" },
};

type_name_fn(encoder_type)

struct type_name connector_status_names[] = {
	{ DRM_MODE_CONNECTED, "connected" },
	{ DRM_MODE_DISCONNECTED, "disconnected" },
	{ DRM_MODE_UNKNOWNCONNECTION, "unknown" },
};

type_name_fn(connector_status)

struct type_name connector_type_names[] = {
	{ DRM_MODE_CONNECTOR_Unknown, "unknown" },
	{ DRM_MODE_CONNECTOR_VGA, "VGA" },
	{ DRM_MODE_CONNECTOR_DVII, "DVI-I" },
	{ DRM_MODE_CONNECTOR_DVID, "DVI-D" },
	{ DRM_MODE_CONNECTOR_DVIA, "DVI-A" },
	{ DRM_MODE_CONNECTOR_Composite, "composite" },
	{ DRM_MODE_CONNECTOR_SVIDEO, "s-video" },
	{ DRM_MODE_CONNECTOR_LVDS, "LVDS" },
	{ DRM_MODE_CONNECTOR_Component, "component" },
	{ DRM_MODE_CONNECTOR_9PinDIN, "9-pin DIN" },
	{ DRM_MODE_CONNECTOR_DisplayPort, "displayport" },
	{ DRM_MODE_CONNECTOR_HDMIA, "HDMI-A" },
	{ DRM_MODE_CONNECTOR_HDMIB, "HDMI-B" },
	{ DRM_MODE_CONNECTOR_TV, "TV" },
	{ DRM_MODE_CONNECTOR_eDP, "embedded displayport" },
};

type_name_fn(connector_type)

#define bit_name_fn(res)					\
char * res##_str(int type) {					\
	int i;							\
	const char *sep = "";					\
	for (i = 0; i < ARRAY_SIZE(res##_names); i++) {		\
		if (type & (1 << i)) {				\
			printf("%s%s", sep, res##_names[i]);	\
			sep = ", ";				\
		}						\
	}							\
	return NULL;						\
}

static const char *mode_type_names[] = {
	"builtin",
	"clock_c",
	"crtc_c",
	"preferred",
	"default",
	"userdef",
	"driver",
};

bit_name_fn(mode_type)

static const char *mode_flag_names[] = {
	"phsync",
	"nhsync",
	"pvsync",
	"nvsync",
	"interlace",
	"dblscan",
	"csync",
	"pcsync",
	"ncsync",
	"hskew",
	"bcast",
	"pixmux",
	"dblclk",
	"clkdiv2"
};

bit_name_fn(mode_flag)

void dump_encoders(void)
{
	drmModeEncoder *encoder;
	int i;

	printf("Encoders:\n");
	printf("id\tcrtc\ttype\tpossible crtcs\tpossible clones\t\n");
	for (i = 0; i < resources->count_encoders; i++) {
		encoder = drmModeGetEncoder(fd, resources->encoders[i]);

		if (!encoder) {
			fprintf(stderr, "could not get encoder %i: %s\n",
				resources->encoders[i], strerror(errno));
			continue;
		}
		printf("%d\t%d\t%s\t0x%08x\t0x%08x\n",
		       encoder->encoder_id,
		       encoder->crtc_id,
		       encoder_type_str(encoder->encoder_type),
		       encoder->possible_crtcs,
		       encoder->possible_clones);
		drmModeFreeEncoder(encoder);
	}
	printf("\n");
}

void dump_mode(drmModeModeInfo *mode)
{
	printf("  %s %d %d %d %d %d %d %d %d %d",
	       mode->name,
	       mode->vrefresh,
	       mode->hdisplay,
	       mode->hsync_start,
	       mode->hsync_end,
	       mode->htotal,
	       mode->vdisplay,
	       mode->vsync_start,
	       mode->vsync_end,
	       mode->vtotal);

	printf(" flags: ");
	mode_flag_str(mode->flags);
	printf("; type: ");
	mode_type_str(mode->type);
	printf("\n");
}

static void
dump_blob(uint32_t blob_id)
{
	uint32_t i;
	unsigned char *blob_data;
	drmModePropertyBlobPtr blob;

	blob = drmModeGetPropertyBlob(fd, blob_id);
	if (!blob)
		return;

	blob_data = blob->data;

	for (i = 0; i < blob->length; i++) {
		if (i % 16 == 0)
			printf("\n\t\t\t");
		printf("%.2hhx", blob_data[i]);
	}
	printf("\n");

	drmModeFreePropertyBlob(blob);
}

static void
dump_prop(uint32_t prop_id, uint64_t value)
{
	int i;
	drmModePropertyPtr prop;

	prop = drmModeGetProperty(fd, prop_id);

	printf("\t%d", prop_id);
	if (!prop) {
		printf("\n");
		return;
	}

	printf(" %s:\n", prop->name);

	printf("\t\tflags:");
	if (prop->flags & DRM_MODE_PROP_PENDING)
		printf(" pending");
	if (prop->flags & DRM_MODE_PROP_RANGE)
		printf(" range");
	if (prop->flags & DRM_MODE_PROP_IMMUTABLE)
		printf(" immutable");
	if (prop->flags & DRM_MODE_PROP_ENUM)
		printf(" enum");
	if (prop->flags & DRM_MODE_PROP_BITMASK)
		printf(" bitmask");
	if (prop->flags & DRM_MODE_PROP_BLOB)
		printf(" blob");
	printf("\n");

	if (prop->flags & DRM_MODE_PROP_RANGE) {
		printf("\t\tvalues:");
		for (i = 0; i < prop->count_values; i++)
			printf(" %"PRIu64, prop->values[i]);
		printf("\n");
	}

	if (prop->flags & DRM_MODE_PROP_ENUM) {
		printf("\t\tenums:");
		for (i = 0; i < prop->count_enums; i++)
			printf(" %s=%llu", prop->enums[i].name,
			       prop->enums[i].value);
		printf("\n");
	} else if (prop->flags & DRM_MODE_PROP_BITMASK) {
		printf("\t\tvalues:");
		for (i = 0; i < prop->count_enums; i++)
			printf(" %s=0x%llx", prop->enums[i].name,
			       (1LL << prop->enums[i].value));
		printf("\n");
	} else {
		assert(prop->count_enums == 0);
	}

	if (prop->flags & DRM_MODE_PROP_BLOB) {
		printf("\t\tblobs:\n");
		for (i = 0; i < prop->count_blobs; i++)
			dump_blob(prop->blob_ids[i]);
		printf("\n");
	} else {
		assert(prop->count_blobs == 0);
	}

	printf("\t\tvalue:");
	if (prop->flags & DRM_MODE_PROP_BLOB)
		dump_blob(value);
	else
		printf(" %"PRIu64"\n", value);

	drmModeFreeProperty(prop);
}

void dump_connectors(void)
{
	drmModeConnector *connector;
	int i, j;

	printf("Connectors:\n");
	printf("id\tencoder\tstatus\t\ttype\tsize (mm)\tmodes\tencoders\n");
	for (i = 0; i < resources->count_connectors; i++) {
		connector = drmModeGetConnector(fd, resources->connectors[i]);

		if (!connector) {
			fprintf(stderr, "could not get connector %i: %s\n",
				resources->connectors[i], strerror(errno));
			continue;
		}

		printf("%d\t%d\t%s\t%s\t%dx%d\t\t%d\t",
		       connector->connector_id,
		       connector->encoder_id,
		       connector_status_str(connector->connection),
		       connector_type_str(connector->connector_type),
		       connector->mmWidth, connector->mmHeight,
		       connector->count_modes);

		for (j = 0; j < connector->count_encoders; j++)
			printf("%s%d", j > 0 ? ", " : "", connector->encoders[j]);
		printf("\n");

		if (connector->count_modes) {
			printf("  modes:\n");
			printf("\tname refresh (Hz) hdisp hss hse htot vdisp "
			       "vss vse vtot)\n");
			for (j = 0; j < connector->count_modes; j++)
				dump_mode(&connector->modes[j]);

			printf("  props:\n");
			for (j = 0; j < connector->count_props; j++)
				dump_prop(connector->props[j],
					  connector->prop_values[j]);
		}

		drmModeFreeConnector(connector);
	}
	printf("\n");
}

void dump_crtcs(void)
{
	drmModeCrtc *crtc;
	drmModeObjectPropertiesPtr props;
	int i;
	uint32_t j;

	printf("CRTCs:\n");
	printf("id\tfb\tpos\tsize\n");
	for (i = 0; i < resources->count_crtcs; i++) {
		crtc = drmModeGetCrtc(fd, resources->crtcs[i]);

		if (!crtc) {
			fprintf(stderr, "could not get crtc %i: %s\n",
				resources->crtcs[i], strerror(errno));
			continue;
		}
		printf("%d\t%d\t(%d,%d)\t(%dx%d)\n",
		       crtc->crtc_id,
		       crtc->buffer_id,
		       crtc->x, crtc->y,
		       crtc->width, crtc->height);
		dump_mode(&crtc->mode);

		printf("  props:\n");
		props = drmModeObjectGetProperties(fd, crtc->crtc_id,
						   DRM_MODE_OBJECT_CRTC);
		if (props) {
			for (j = 0; j < props->count_props; j++)
				dump_prop(props->props[j],
					  props->prop_values[j]);
			drmModeFreeObjectProperties(props);
		} else {
			printf("\tcould not get crtc properties: %s\n",
			       strerror(errno));
		}

		drmModeFreeCrtc(crtc);
	}
	printf("\n");
}

void dump_framebuffers(void)
{
	drmModeFB *fb;
	int i;

	printf("Frame buffers:\n");
	printf("id\tsize\tpitch\n");
	for (i = 0; i < resources->count_fbs; i++) {
		fb = drmModeGetFB(fd, resources->fbs[i]);

		if (!fb) {
			fprintf(stderr, "could not get fb %i: %s\n",
				resources->fbs[i], strerror(errno));
			continue;
		}
		printf("%u\t(%ux%u)\t%u\n",
		       fb->fb_id,
		       fb->width, fb->height,
		       fb->pitch);

		drmModeFreeFB(fb);
	}
	printf("\n");
}

static void dump_planes(void)
{
	drmModeObjectPropertiesPtr props;
	drmModePlaneRes *plane_resources;
	drmModePlane *ovr;
	unsigned int i, j;

	plane_resources = drmModeGetPlaneResources(fd);
	if (!plane_resources) {
		fprintf(stderr, "drmModeGetPlaneResources failed: %s\n",
			strerror(errno));
		return;
	}

	printf("Planes:\n");
	printf("id\tcrtc\tfb\tCRTC x,y\tx,y\tgamma size\n");
	for (i = 0; i < plane_resources->count_planes; i++) {
		ovr = drmModeGetPlane(fd, plane_resources->planes[i]);
		if (!ovr) {
			fprintf(stderr, "drmModeGetPlane failed: %s\n",
				strerror(errno));
			continue;
		}

		printf("%d\t%d\t%d\t%d,%d\t\t%d,%d\t%d\n",
		       ovr->plane_id, ovr->crtc_id, ovr->fb_id,
		       ovr->crtc_x, ovr->crtc_y, ovr->x, ovr->y,
		       ovr->gamma_size);

		if (!ovr->count_formats)
			continue;

		printf("  formats:");
		for (j = 0; j < ovr->count_formats; j++)
			printf(" %4.4s", (char *)&ovr->formats[j]);
		printf("\n");

		printf("  props:\n");
		props = drmModeObjectGetProperties(fd, ovr->plane_id,
						   DRM_MODE_OBJECT_PLANE);
		if (props) {
			for (j = 0; j < props->count_props; j++)
				dump_prop(props->props[j],
					  props->prop_values[j]);
			drmModeFreeObjectProperties(props);
		} else {
			printf("\tcould not get plane properties: %s\n",
			       strerror(errno));
		}

		drmModeFreePlane(ovr);
	}
	printf("\n");

	drmModeFreePlaneResources(plane_resources);
	return;
}

/* -----------------------------------------------------------------------------
 * Connectors and planes
 */

/*
 * Mode setting with the kernel interfaces is a bit of a chore.
 * First you have to find the connector in question and make sure the
 * requested mode is available.
 * Then you need to find the encoder attached to that connector so you
 * can bind it with a free crtc.
 */
struct connector {
	uint32_t id;
	char mode_str[64];
	uint32_t w, h;
	char format_str[5];
	unsigned int fourcc;
	drmModeModeInfo *mode;
	drmModeEncoder *encoder;
	int crtc;
	int pipe;
	unsigned int fb_id[2], current_fb_id;
	struct timeval start;

	int swap_count;
};

struct plane {
	uint32_t con_id;  /* the id of connector to bind to */
	uint32_t w, h;
	unsigned int fb_id;
	char format_str[5]; /* need to leave room for terminating \0 */
	unsigned int fourcc;
};

static void
connector_find_mode(struct connector *c)
{
	drmModeConnector *connector;
	int i, j;

	/* First, find the connector & mode */
	c->mode = NULL;
	for (i = 0; i < resources->count_connectors; i++) {
		connector = drmModeGetConnector(fd, resources->connectors[i]);

		if (!connector) {
			fprintf(stderr, "could not get connector %i: %s\n",
				resources->connectors[i], strerror(errno));
			drmModeFreeConnector(connector);
			continue;
		}

		if (!connector->count_modes) {
			drmModeFreeConnector(connector);
			continue;
		}

		if (connector->connector_id != c->id) {
			drmModeFreeConnector(connector);
			continue;
		}

		for (j = 0; j < connector->count_modes; j++) {
			c->mode = &connector->modes[j];
			if (!strcmp(c->mode->name, c->mode_str))
				break;
		}

		/* Found it, break out */
		if (c->mode)
			break;

		drmModeFreeConnector(connector);
	}

	if (!c->mode) {
		fprintf(stderr, "failed to find mode \"%s\"\n", c->mode_str);
		return;
	}

	/* Now get the encoder */
	for (i = 0; i < resources->count_encoders; i++) {
		c->encoder = drmModeGetEncoder(fd, resources->encoders[i]);

		if (!c->encoder) {
			fprintf(stderr, "could not get encoder %i: %s\n",
				resources->encoders[i], strerror(errno));
			drmModeFreeEncoder(c->encoder);
			continue;
		}

		if (c->encoder->encoder_id  == connector->encoder_id)
			break;

		drmModeFreeEncoder(c->encoder);
	}

	if (c->crtc == -1)
		c->crtc = c->encoder->crtc_id;

	/* and figure out which crtc index it is: */
	for (i = 0; i < resources->count_crtcs; i++) {
		if (c->crtc == resources->crtcs[i]) {
			c->pipe = i;
			break;
		}
	}

}

/* -------------------------------------------------------------------------- */

static int
set_plane(struct kms_driver *kms, struct connector *c, struct plane *p)
{
	drmModePlaneRes *plane_resources;
	drmModePlane *ovr;
	uint32_t handles[4], pitches[4], offsets[4] = {0}; /* we only use [0] */
	uint32_t plane_id = 0;
	struct kms_bo *plane_bo;
	uint32_t plane_flags = 0;
	int ret, crtc_x, crtc_y, crtc_w, crtc_h;
	unsigned int i;

	/* find an unused plane which can be connected to our crtc */
	plane_resources = drmModeGetPlaneResources(fd);
	if (!plane_resources) {
		fprintf(stderr, "drmModeGetPlaneResources failed: %s\n",
			strerror(errno));
		return -1;
	}
	for (i = 0; i < plane_resources->count_planes && !plane_id; i++) {
		ovr = drmModeGetPlane(fd, plane_resources->planes[i]);
		if (!ovr) {
			fprintf(stderr, "drmModeGetPlane failed: %s\n",
				strerror(errno));
			return -1;
		}

		if ((ovr->possible_crtcs & (1 << c->pipe))) {
			plane_id = ovr->plane_id;
		}

		drmModeFreePlane(ovr);
	}

	fprintf(stderr, "testing %dx%d@%s overlay plane\n",
			p->w, p->h, p->format_str);

	if (!plane_id) {
		fprintf(stderr, "failed to find plane!\n");
		return -1;
	}

	if (!p->fb_id) {
		plane_bo = create_test_buffer(kms, p->fourcc, p->w, p->h, handles,
				pitches, offsets, PATTERN_TILES);
		if (plane_bo == NULL)
			return -1;

		/* just use single plane format for now.. */
		if (drmModeAddFB2(fd, p->w, p->h, p->fourcc,
					handles, pitches, offsets, &p->fb_id, plane_flags)) {
			fprintf(stderr, "failed to add fb: %s\n", strerror(errno));
			return -1;
		}
	}

	/* ok, boring.. but for now put in middle of screen: */
	crtc_x = c->mode->hdisplay / 3;
	crtc_y = c->mode->vdisplay / 3;
	crtc_w = crtc_x;
	crtc_h = crtc_y;

	/* note src coords (last 4 args) are in Q16 format */
	if (drmModeSetPlane(fd, plane_id, c->crtc, p->fb_id,
			    plane_flags, crtc_x, crtc_y, crtc_w, crtc_h,
			    0, 0, p->w << 16, p->h << 16)) {
		fprintf(stderr, "failed to enable plane: %s\n",
			strerror(errno));
		return -1;
	}

	drmModeFreePlaneResources(plane_resources);

	return 0;
}

static void
set_mode(struct connector *c, int count, struct plane *p, int plane_count,
		int test_tpg)
{
	struct kms_driver *kms;
	struct kms_bo *bo, *other_bo;
	unsigned int fb_id = 0, other_fb_id;
	int i, j, ret, width, height, x;
	uint32_t handles[4], pitches[4], offsets[4] = {0}; /* we only use [0] */
	drmEventContext evctx;

	int loop_count = 0;
	void *virtual;

	width = 0;
	height = 0;
	for (i = 0; i < count; i++) {
		connector_find_mode(&c[i]);
		if (c[i].mode == NULL)
			continue;
		width += c[i].mode->hdisplay;
		if (height < c[i].mode->vdisplay)
			height = c[i].mode->vdisplay;
	}

	ret = kms_create(fd, &kms);
	if (ret) {
		fprintf(stderr, "failed to create kms driver: %s\n",
				strerror(-ret));
		return;
	}

	bo = create_test_buffer(kms, c->fourcc, width, height, handles,
			pitches, offsets, (enum fill_pattern)(loop_count % 3));
	if (bo == NULL)
		return;


	while (1) {
		if (test_tpg) {
			set_xlnx_tpg(c->w, c->h, loop_count);
			reset_xlnx_vdma();
			kms_bo_map(bo, &virtual);
			configure_and_start_xlnx_vdma(c->w, c->h, virtual);
			kms_bo_unmap(bo);
		} else {
			/* vmda may not be in reset from previous run, so reset to make sure */
			fill_test_buffer(kms, c->fourcc, width, height, handles, pitches, offsets,
					loop_count % 3, bo, NULL);
		}

		printf("--> enter to update fb\n");
		getchar();

		if (!fb_id) {
			ret = drmModeAddFB2(fd, width, height, c->fourcc,
					handles, pitches, offsets, &fb_id, 0);
			if (ret) {
				fprintf(stderr, "failed to add fb (%ux%u): %s\n",
						width, height, strerror(errno));
				return;
			}
		}

		x = 0;
		for (i = 0; i < count; i++) {
			if (c[i].mode == NULL)
				continue;

			ret = drmModeSetCrtc(fd, c[i].crtc, fb_id, x, 0,
					&c[i].id, 1, c[i].mode);

			/* XXX: Actually check if this is needed */
			drmModeDirtyFB(fd, fb_id, NULL, 0);

			x += c[i].mode->hdisplay;

			if (ret) {
				fprintf(stderr, "failed to set mode: %s\n", strerror(errno));
				return;
			}

			/* if we have a plane/overlay to show, set that up now: */
			for (j = 0; j < plane_count; j++)
				if (p[j].con_id == c[i].id)
					if (set_plane(kms, &c[i], &p[j]))
						return;
		}
		loop_count++;
	}

	printf("--> enter to exit\n");
	getchar();
	kms_bo_destroy(&bo);
	return;
}

extern char *optarg;
extern int optind, opterr, optopt;
static char optstr[] = "ecpmfts:P:v";

#define min(a, b)	((a) < (b) ? (a) : (b))

static int parse_connector(struct connector *c, const char *arg)
{
	unsigned int len;
	const char *p;
	char *endp;
	char temp_mode_str[64];

	c->crtc = -1;
	strcpy(c->format_str, "XR24");

	c->id = strtoul(arg, &endp, 10);
	if (*endp == '@') {
		arg = endp + 1;
		c->crtc = strtoul(arg, &endp, 10);
	}
	if (*endp != ':')
		return -1;

	arg = endp + 1;

	p = strchrnul(arg, '@');
	len = min(sizeof c->mode_str - 1, p - arg);
	strncpy(c->mode_str, arg, len);
	c->mode_str[len] = '\0';
	memcpy(temp_mode_str, c->mode_str, len);
	if (sscanf(temp_mode_str, "%dx%d", &c->w, &c->h) != 2) {
		fprintf(stderr, "fail to parse %s\n", c->mode_str);
		return -1;
	}

	if (*p == '@') {
		strncpy(c->format_str, p + 1, 4);
		c->format_str[4] = '\0';
	}

	c->fourcc = format_fourcc(c->format_str);
	if (c->fourcc == 0)  {
		fprintf(stderr, "unknown format %s\n", c->format_str);
		return -1;
	}

	return 0;
}

static int parse_plane(struct plane *p, const char *arg)
{
	strcpy(p->format_str, "XR24");

	if (sscanf(arg, "%d:%dx%d@%4s", &p->con_id, &p->w, &p->h, &p->format_str) != 4 &&
	    sscanf(arg, "%d:%dx%d", &p->con_id, &p->w, &p->h) != 3)
		return -1;

	p->fourcc = format_fourcc(p->format_str);
	if (p->fourcc == 0) {
		fprintf(stderr, "unknown format %s\n", p->format_str);
		return -1;
	}

	return 0;
}

void usage(char *name)
{
	fprintf(stderr, "usage: %s [-ecpmft]\n", name);
	fprintf(stderr, "\t-e\tlist encoders\n");
	fprintf(stderr, "\t-c\tlist connectors\n");
	fprintf(stderr, "\t-p\tlist CRTCs and planes (pipes)\n");
	fprintf(stderr, "\t-m\tlist modes\n");
	fprintf(stderr, "\t-f\tlist framebuffers\n");
	fprintf(stderr, "\t-v\ttest vsynced page flipping\n");
	fprintf(stderr, "\t-s <connector_id>[@<crtc_id>]:<mode>[@<format>]\tset a mode\n");
	fprintf(stderr, "\t-P <connector_id>:<w>x<h>[@<format>]\tset a plane\n");
	fprintf(stderr, "\t-t \tenable tpg(1080p only)\n");
	fprintf(stderr, "\n\tDefault is to dump all info.\n");
	exit(0);
}

#define dump_resource(res) if (res) dump_##res()

int main(int argc, char **argv)
{
	int c;
	int encoders = 0, connectors = 0, crtcs = 0, planes = 0, framebuffers = 0;
	int test_tpg = 0;
	char *modules[] = { "zynq_drm" };
	unsigned int i;
	int count = 0, plane_count = 0;
	struct connector con_args[2];
	struct plane plane_args[2] = {0};

	opterr = 0;
	while ((c = getopt(argc, argv, optstr)) != -1) {
		switch (c) {
		case 'e':
			encoders = 1;
			break;
		case 'c':
			connectors = 1;
			break;
		case 'p':
			crtcs = 1;
			planes = 1;
			break;
		case 'm':
			modes = 1;
			break;
		case 'f':
			framebuffers = 1;
			break;
		case 's':
			if (parse_connector(&con_args[count], optarg) < 0)
				usage(argv[0]);
			count++;
			break;
		case 'P':
			if (parse_plane(&plane_args[plane_count], optarg) < 0)
				usage(argv[0]);
			plane_count++;
			break;
		case 't':
			test_tpg = 1;
			break;
		default:
			usage(argv[0]);
			break;
		}
	}

	if (argc == 1)
		encoders = connectors = crtcs = planes = modes = framebuffers = 1;

	for (i = 0; i < ARRAY_SIZE(modules); i++) {
		printf("trying to load module %s...", modules[i]);
		fd = drmOpen(modules[i], NULL);
		if (fd < 0) {
			printf("failed.\n");
		} else {
			printf("success.\n");
			break;
		}
	}

	if (i == ARRAY_SIZE(modules)) {
		fprintf(stderr, "failed to load any modules, aborting.\n");
		return -1;
	}

	resources = drmModeGetResources(fd);
	if (!resources) {
		fprintf(stderr, "drmModeGetResources failed: %s\n",
			strerror(errno));
		drmClose(fd);
		return 1;
	}

	dump_resource(encoders);
	dump_resource(connectors);
	dump_resource(crtcs);
	dump_resource(planes);
	dump_resource(framebuffers);

	if (count > 0) {
		set_mode(con_args, count, plane_args, plane_count, test_tpg);
		getchar();
	}

	drmModeFreeResources(resources);

	return 0;
}
