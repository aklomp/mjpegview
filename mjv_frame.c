#include <time.h>	// clock_gettime()
#include <errno.h>
#include <stdlib.h>	// malloc(), free()
#include <stdio.h>	// FILE, for jpeglib.h
#include <string.h>	// memcpy()
#include <jpeglib.h>
#include <glib.h>
#include <assert.h>
#include <jerror.h>
#include <setjmp.h>

const char *err_malloc  = "Memory allocation failed";
const char *err_unknown = "Unknown error";

struct mjv_frame {
	struct timespec *timestamp;
	char *error;
	unsigned char *rawbits;
	unsigned int num_rawbits;
	unsigned int width;
	unsigned int height;
	unsigned int row_stride;
	unsigned int components;
	unsigned int successfully_decoded;
};

struct my_jpeg_error_mgr {
	struct jpeg_error_mgr pub;
	jmp_buf setjmp_buffer;
};

struct mjv_frame *
mjv_frame_create (char *rawbits, unsigned int num_rawbits)
{
	struct mjv_frame *f = NULL;
	struct timespec timestamp;

	// First thing, timestamp this frame:
	if (clock_gettime(CLOCK_REALTIME, &timestamp) != 0) {
		g_printerr(g_strerror(errno));
		goto err;
	}
	// Allocate structure:
	if ((f = malloc(sizeof(*f))) == NULL) {
		return NULL;
	}
	// Allocate timestamp struct:
	if ((f->timestamp = malloc(sizeof(*f->timestamp))) == NULL) {
		goto err;
	}
	// Allocate space for the frame:
	if ((f->rawbits = malloc(num_rawbits)) == NULL) {
		goto err;
	}
	// Copy timestamp over:
	memcpy(f->timestamp, &timestamp, sizeof(struct timespec));

	// Copy rawbits over:
	memcpy(f->rawbits, rawbits, num_rawbits);

	// Set default values:
	f->num_rawbits = num_rawbits;
	f->error = NULL;
	f->width = 0;
	f->height = 0;
	f->successfully_decoded = 0;

	// Successful return:
	return f;

err:	if (f != NULL) {
		free(f->error);
		free(f->rawbits);
		free(f->timestamp);
		free(f);
	}
	return NULL;
}

void
mjv_frame_destroy (struct mjv_frame *f)
{
	if (f != NULL) {
		free(f->error);
		free(f->rawbits);
		free(f->timestamp);
		free(f);
	}
}

// Trivial callback function when libjpeg encounters an error:
static void
on_jpeg_error (j_common_ptr cinfo)
{
	// Jump back to original function:
	longjmp(((struct my_jpeg_error_mgr *)(cinfo->err))->setjmp_buffer, 1);
}

unsigned char *
mjv_frame_to_pixbuf (struct mjv_frame *f)
{
	unsigned char *pixbuf;
	struct jpeg_decompress_struct cinfo;
	struct my_jpeg_error_mgr jerr;
	JSAMPROW row_pointer;

	// Given a mjv_frame, returns a pixmap and sets some of the feame's
	// variables, such as height and width (which are unknown till we
	// actually decode the image).
	// Caaller is responsible for freeing the resulting pixmap.

	assert(f != NULL);

	// Use a custom error exit; libjpeg just calls exit()
	cinfo.err = jpeg_std_error( &jerr.pub );
	jerr.pub.error_exit = on_jpeg_error;
	if (setjmp(jerr.setjmp_buffer)) {
		char jpeg_errmsg[JMSG_LENGTH_MAX];
		jerr.pub.format_message((j_common_ptr)(&cinfo), jpeg_errmsg);
		size_t len = strlen(jpeg_errmsg) + 1;
		f->error = g_realloc(f->error, len);
		memcpy(f->error, jpeg_errmsg, len);
		jpeg_destroy_decompress(&cinfo);
		f->successfully_decoded = 0;
		return NULL;
	}
	jpeg_create_decompress(&cinfo);

	// Note: this source is available from libjpeg v8 onwards:
	jpeg_mem_src(&cinfo, f->rawbits, f->num_rawbits);

	// FIXME error checking??
	jpeg_read_header(&cinfo, TRUE);
	jpeg_start_decompress(&cinfo);

	f->row_stride = cinfo.output_width * cinfo.output_components;

	// Update the frame object with this new information:
	f->height = cinfo.output_height;
	f->width = cinfo.output_width;
	f->components = cinfo.output_components;

	// Allocate our own output buffer:
	// FIXME: constant allocating/freeing is wasteful;
	// maybe reuse the pixmap and only realloc if the dimensions change:
	if ((pixbuf = malloc(cinfo.output_height * f->row_stride)) == NULL) {
		return NULL;
	}
	// FIXME error checking or somehting!!
	while (cinfo.output_scanline < cinfo.output_height) {
		row_pointer = &pixbuf[cinfo.output_scanline * f->row_stride];
		jpeg_read_scanlines(&cinfo, &row_pointer, 1);
	}
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	f->successfully_decoded = 1;

	return pixbuf;
}

unsigned int
mjv_frame_get_width (const struct mjv_frame *const frame)
{
	assert(frame != NULL);
	return frame->width;
}

unsigned int
mjv_frame_get_height (const struct mjv_frame *const frame)
{
	assert(frame != NULL);
	return frame->height;
}

unsigned int
mjv_frame_get_row_stride (const struct mjv_frame *const frame)
{
	g_assert(frame != NULL);
	return frame->row_stride;
}
