/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / ICNS (Apple icon image) decoder filter, based
 *  on libicns (https://sourceforge.net/projects/icns/).
 *
 *  An .icns file is a container holding the same icon at several sizes and
 *  depths; the largest 32-bit variant available is decoded, with its mask
 *  applied, and output as RGBA.
 */

#include <gpac/filters.h>
#include <gpac/constants.h>
#include <string.h>
#include <stdlib.h>

#include <icns.h>

typedef struct
{
	GF_FilterPid *ipid, *opid;
	Bool is_playing;
} GF_ICNSDecCtx;

/* Every 32-bit icon type, largest physical size first (the _2X entries are the
 * retina variants, twice their nominal size): the first one present in the file
 * wins. Both spellings of the 128x128 type are here on purpose - the modern
 * ARGB one and ICNS_128X128_32BIT_DATA, which is the classic "it32" element
 * that older .icns files carry. */
static const icns_type_t icns_preferred_types[] = {
	ICNS_1024x1024_32BIT_ARGB_DATA,
	ICNS_512x512_2X_32BIT_ARGB_DATA,
	ICNS_512x512_32BIT_ARGB_DATA,
	ICNS_256x256_2X_32BIT_ARGB_DATA,
	ICNS_256x256_32BIT_ARGB_DATA,
	ICNS_128x128_2X_32BIT_ARGB_DATA,
	ICNS_128x128_32BIT_ARGB_DATA,
	ICNS_128X128_32BIT_DATA,
	ICNS_64x64_32BIT_ARGB_DATA,
	ICNS_48x48_32BIT_DATA,
	ICNS_32x32_2X_32BIT_ARGB_DATA,
	ICNS_32x32_32BIT_DATA,
	ICNS_16x16_2X_32BIT_ARGB_DATA,
	ICNS_16x16_32BIT_DATA};

static GF_Err icnsdec_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	GF_ICNSDecCtx *ctx = (GF_ICNSDecCtx *)gf_filter_get_udta(filter);

	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	ctx->ipid = pid;
	gf_filter_pid_set_framing_mode(pid, GF_TRUE);

	if (!ctx->opid)
		ctx->opid = gf_filter_pid_new(filter);

	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STREAM_TYPE, &PROP_UINT(GF_STREAM_VISUAL));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(GF_CODECID_RAW));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_RGB));

	return GF_OK;
}

static Bool icnsdec_process_event(GF_Filter *filter, const GF_FilterEvent *evt)
{
	GF_ICNSDecCtx *ctx = (GF_ICNSDecCtx *)gf_filter_get_udta(filter);
	switch (evt->base.type)
	{
	case GF_FEVT_PLAY:
		ctx->is_playing = GF_TRUE;
		return GF_FALSE;
	case GF_FEVT_STOP:
		ctx->is_playing = GF_FALSE;
		return GF_FALSE;
	default:
		return GF_FALSE;
	}
}

static GF_Err icnsdec_process(GF_Filter *filter)
{
	GF_FilterPacket *pck, *dst_pck;
	u8 *data, *output;
	u32 size, out_size, i;
	int found = 0;
	icns_family_t *family = NULL;
	icns_image_t image;
	GF_ICNSDecCtx *ctx = (GF_ICNSDecCtx *)gf_filter_get_udta(filter);

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);
	if (!data)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_IO_ERR;
	}

	if (icns_import_family_data(size, (icns_byte_t *)data, &family) != ICNS_STATUS_OK)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[ICNSDec] Not a valid ICNS file\n"));
		return GF_NON_COMPLIANT_BITSTREAM;
	}
	gf_filter_pid_drop_packet(ctx->ipid);

	memset(&image, 0, sizeof(image));
	for (i = 0; i < sizeof(icns_preferred_types) / sizeof(icns_type_t); i++)
	{
		if (icns_get_image32_with_mask_from_family(family, icns_preferred_types[i], &image) == ICNS_STATUS_OK)
		{
			found = 1;
			break;
		}
	}
	free(family);

	if (!found || !image.imageData || !image.imageWidth || !image.imageHeight)
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[ICNSDec] No 32-bit icon found in family\n"));
		return GF_NOT_SUPPORTED;
	}

	/* libicns always yields 8-bit RGBA; the alpha channel is dropped because an
	 * RGBA pid has no adaptation path to writegen in this build (same
	 * limitation as the rgba mode documented in test-player/libpng.js). */
	out_size = image.imageWidth * image.imageHeight * 3;

	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_WIDTH, &PROP_UINT(image.imageWidth));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_HEIGHT, &PROP_UINT(image.imageHeight));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STRIDE, &PROP_UINT(image.imageWidth * 3));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_RGB));

	dst_pck = gf_filter_pck_new_alloc(ctx->opid, out_size, &output);
	if (!dst_pck)
	{
		icns_free_image(&image);
		return GF_OUT_OF_MEM;
	}
	{
		u32 nb_pixels = image.imageWidth * image.imageHeight, i;
		for (i = 0; i < nb_pixels; i++)
			memcpy(output + i * 3, image.imageData + i * 4, 3);
	}
	icns_free_image(&image);

	gf_filter_pck_set_cts(dst_pck, 0);
	gf_filter_pck_set_sap(dst_pck, GF_FILTER_SAP_1);
	gf_filter_pck_send(dst_pck);

	gf_filter_pid_set_eos(ctx->opid);
	return GF_EOS;
}

static void icnsdec_finalize(GF_Filter *filter)
{
}

static const GF_FilterCapability ICNSDecCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_FILE),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_FILE_EXT, "icns"),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_MIME, "image/icns|image/x-icns"),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
};

GF_FilterRegister ICNSDecoderRegister = {
	.name = "icnsdec",
	GF_FS_SET_DESCRIPTION("Apple icon (ICNS) decoder")
		GF_FS_SET_HELP("This filter decodes the largest 32-bit icon of an Apple ICNS file using libicns.")
			.private_size = sizeof(GF_ICNSDecCtx),
	SETCAPS(ICNSDecCaps),
	.configure_pid = icnsdec_configure_pid,
	.process = icnsdec_process,
	.process_event = icnsdec_process_event,
	.finalize = icnsdec_finalize,
};

const GF_FilterRegister *EMSCRIPTEN_KEEPALIVE icnsdec_register(GF_FilterSession *session)
{
	return &ICNSDecoderRegister;
}

#include "filter_register.h"
__attribute__((constructor))
void register_icnsdec(void) {
    gf_filter_auto_register("icnsdec", icnsdec_register);
}
