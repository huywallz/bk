/*
bk_png.h - PNG image loader for the Brickate project

This header provides functionality to load and decode PNG images
with minimal dependencies.

Features:
  - Parses PNG headers and verifies signature
  - Supports critical chunks: IHDR, PLTE, IDAT, IEND
  - Optional support for gAMA chunk
  - Performs CRC validation on all chunks
  - Collects IDAT data for later decompression
  - Reads palette data and image gamma if present

Intended for use in software rasterizers or custom game engines
where lightweight image loading is preferred.
*/

#ifndef BK_PNG_H
#define BK_PNG_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <zlib.h>
#include <assert.h>

// PNG color types
#define BK_PNG_GRAY 0
#define BK_PNG_GRAY_ALPHA 4
#define BK_PNG_RGB 2
#define BK_PNG_INDEXED 3
#define BK_PNG_RGBA 6

typedef struct {
	uint32_t width;
	uint32_t height;
	uint8_t bit_depth;
	uint8_t color_type;
	uint8_t compression_method;
	uint8_t filter_method;
	uint8_t interlace_method;
} bkp_ihdr;

typedef struct {
	unsigned char palette[256][4]; // RGBA palette entries
	size_t size;
} bkp_palette;

typedef struct {
	unsigned char* data;
	size_t size;
} bkp_buffer;

uint32_t bkp_crc32(uint32_t crc, const unsigned char* buf, size_t len) {
	static uint32_t table[256];
	static int have_table = 0;

	if (!have_table) {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t rem = i;
			for (int j = 0; j < 8; j++) {
				if (rem & 1) rem = (rem >> 1) ^ 0xEDB88320;
				else rem >>= 1;
			}
			table[i] = rem;
		}
		have_table = 1;
	}

	crc = ~crc;
	for (size_t i = 0; i < len; i++) {
		crc = (crc >> 8) ^ table[(crc ^ buf[i]) & 0xFF];
	}
	return ~crc;
}

int bkp_read_be32(FILE* f, uint32_t* out) {
	unsigned char b[4];
	if (fread(b, 1, 4, f) != 4) return 0;
	*out = (b[0]<<24) | (b[1]<<16) | (b[2]<<8) | b[3];
	return 1;
}

int bkp_read_chunk_header(FILE* f, uint32_t* length, char type[5]) {
	if (!bkp_read_be32(f, length)) return 0;
	if (fread(type, 1, 4, f) != 4) return 0;
	type[4] = 0;
	return 1;
}

int bkp_read_ihdr(FILE* f, bkp_ihdr* ihdr) {
	uint32_t length;
	char type[5];

	if (!bkp_read_chunk_header(f, &length, type)) return 0;
	if (strcmp(type, "IHDR") != 0) return 0;
	if (length != 13) return 0;

	unsigned char data[13];
	if (fread(data, 1, 13, f) != 13) return 0;

	uint32_t crc_read;
	if (!bkp_read_be32(f, &crc_read)) return 0;

	unsigned char crc_buf[17];
	memcpy(crc_buf, type, 4);
	memcpy(crc_buf + 4, data, 13);
	if (bkp_crc32(0, crc_buf, 17) != crc_read) return 0;

	ihdr->width = (data[0]<<24) | (data[1]<<16) | (data[2]<<8) | data[3];
	ihdr->height = (data[4]<<24) | (data[5]<<16) | (data[6]<<8) | data[7];
	ihdr->bit_depth = data[8];
	ihdr->color_type = data[9];
	ihdr->compression_method = data[10];
	ihdr->filter_method = data[11];
	ihdr->interlace_method = data[12];

	if (ihdr->bit_depth != 8) return 0; // support 8-bit only
	if (ihdr->compression_method != 0) return 0;
	if (ihdr->filter_method != 0) return 0;
	if (ihdr->interlace_method != 0) return 0;

	return 1;
}

int bkp_read_plte(FILE* f, uint32_t length, bkp_palette* pal) {
	if (length % 3 != 0) return 0;
	size_t n = length / 3;
	if (n > 256) return 0;

	unsigned char* data = malloc(length);
	if (!data) return 0;
	if (fread(data, 1, length, f) != length) {
		free(data);
		return 0;
	}

	uint32_t crc_read;
	if (!bkp_read_be32(f, &crc_read)) {
		free(data);
		return 0;
	}

	unsigned char crc_buf[length + 4];
	memcpy(crc_buf, "PLTE", 4);
	memcpy(crc_buf + 4, data, length);
	if (bkp_crc32(0, crc_buf, length + 4) != crc_read) {
		free(data);
		return 0;
	}

	for (size_t i = 0; i < n; i++) {
		pal->palette[i][0] = data[i*3+0];
		pal->palette[i][1] = data[i*3+1];
		pal->palette[i][2] = data[i*3+2];
		pal->palette[i][3] = 255;
	}
	pal->size = n;

	free(data);
	return 1;
}

int bkp_collect_idat_data(FILE* f, bkp_buffer* idat_buf, uint32_t length) {
	// Allocate buffer for chunk data
	unsigned char* chunk_data = malloc(length);
	if (!chunk_data) {
		free(idat_buf->data);
		return 0;
	}

	// Read chunk data
	if (fread(chunk_data, 1, length, f) != length) {
		free(chunk_data);
		free(idat_buf->data);
		return 0;
	}

	// Read CRC
	uint32_t crc_read;
	if (!bkp_read_be32(f, &crc_read)) {
		free(chunk_data);
		free(idat_buf->data);
		return 0;
	}

	// Prepare CRC buffer: chunk type "IDAT" + chunk_data
	unsigned char crc_buf[length + 4];
	memcpy(crc_buf, "IDAT", 4);
	memcpy(crc_buf + 4, chunk_data, length);

	// Verify CRC
	if (bkp_crc32(0, crc_buf, length + 4) != crc_read) {
		free(chunk_data);
		free(idat_buf->data);
		return 0;
	}

	// Append chunk_data to idat_buf->data
	unsigned char* new_data = realloc(idat_buf->data, idat_buf->size + length);
	if (!new_data) {
		free(chunk_data);
		free(idat_buf->data);
		return 0;
	}
	idat_buf->data = new_data;
	memcpy(idat_buf->data + idat_buf->size, chunk_data, length);
	idat_buf->size += length;

	free(chunk_data);
	return 1;
}

unsigned char* bkp_decompress_zlib(const unsigned char* compressed, size_t compressed_size, size_t* out_size) {
	size_t max_out = compressed_size * 4;
	unsigned char* out = malloc(max_out);
	if (!out) return NULL;

	z_stream strm = {0};
	strm.next_in = (unsigned char*)compressed;
	strm.avail_in = compressed_size;
	strm.next_out = out;
	strm.avail_out = max_out;

	if (inflateInit(&strm) != Z_OK) {
		free(out);
		return NULL;
	}
	
	int ret;
	do {
		ret = inflate(&strm, Z_NO_FLUSH);
		if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
			inflateEnd(&strm);
			free(out);
			return NULL;
		}
		if (strm.avail_out == 0) {
			size_t old_size = strm.total_out;
			unsigned char* new_out = realloc(out, old_size * 2);
			if (!new_out) {
				inflateEnd(&strm);
				free(out);
				return NULL;
			}
			out = new_out;
			strm.next_out = out + old_size;
			strm.avail_out = old_size;
		}
	} while (ret != Z_STREAM_END);
	
	*out_size = strm.total_out;
	inflateEnd(&strm);
	return out;
}

unsigned char bkp_paeth_predictor(int a, int b, int c) {
	int p = a + b - c;
	int pa = abs(p - a);
	int pb = abs(p - b);
	int pc = abs(p - c);

	if (pa <= pb && pa <= pc) return a;
	if (pb <= pc) return b;
	return c;
}

int bkp_filter_decode(const unsigned char* data, int width, int height, int bpp, unsigned char* out) {
	const int stride = width * bpp;
	const unsigned char* prev_row = NULL;
	const unsigned char* curr_ptr = data;

	for (int y = 0; y < height; y++) {
		unsigned char filter = *curr_ptr++;
		unsigned char* out_row = out + y * stride;

		switch (filter) {
			case 0: // None
				memcpy(out_row, curr_ptr, stride);
				break;
			case 1: // Sub
				for (int i = 0; i < stride; i++) {
					unsigned char left = (i >= bpp) ? out_row[i - bpp] : 0;
					out_row[i] = curr_ptr[i] + left;
				}
				break;
			case 2: // Up
				for (int i = 0; i < stride; i++) {
					unsigned char up = prev_row ? prev_row[i] : 0;
					out_row[i] = curr_ptr[i] + up;
				}
				break;
			case 3: // Average
				for (int i = 0; i < stride; i++) {
					unsigned char left = (i >= bpp) ? out_row[i - bpp] : 0;
					unsigned char up = prev_row ? prev_row[i] : 0;
					out_row[i] = curr_ptr[i] + ((left + up) >> 1);
				}
				break;
			case 4: // Paeth
				for (int i = 0; i < stride; i++) {
					unsigned char left = (i >= bpp) ? out_row[i - bpp] : 0;
					unsigned char up = prev_row ? prev_row[i] : 0;
					unsigned char up_left = (prev_row && i >= bpp) ? prev_row[i - bpp] : 0;
					out_row[i] = curr_ptr[i] + bkp_paeth_predictor(left, up, up_left);
				}
				break;
			default:
				return 0;
		}

		prev_row = out_row;
		curr_ptr += stride;
	}

	return 1;
}

void bkp_expand_palette(const unsigned char* indexed, size_t n_pixels, const bkp_palette* pal, unsigned char* out) {
	for (size_t i = 0; i < n_pixels; i++) {
		int idx = indexed[i];
		if (idx >= (int)pal->size) idx = 0;
		out[i*4+0] = pal->palette[idx][0];
		out[i*4+1] = pal->palette[idx][1];
		out[i*4+2] = pal->palette[idx][2];
		out[i*4+3] = pal->palette[idx][3];
	}
}

// Adam7 parameters for each pass:
// Starting x, starting y, x increment, y increment
static const int ADAM7_PASSES[7][4] = {
	{0, 0, 8, 8},
	{4, 0, 8, 8},
	{0, 4, 4, 8},
	{2, 0, 4, 4},
	{0, 2, 2, 4},
	{1, 0, 2, 2},
	{0, 1, 1, 2},
};

int bkp_decode_adam7(const unsigned char* data, int width, int height, int bpp, unsigned char* out) {
	// 'data' contains all interlaced filtered scanlines concatenated

	int pass_sizes[7][2]; // width and height per pass
	int pass_scanline_bytes[7];

	// Calculate pass dimensions
	for (int p = 0; p < 7; p++) {
		int pass_width = 0;
		int pass_height = 0;
		for (int x = ADAM7_PASSES[p][0]; x < width; x += ADAM7_PASSES[p][2]) pass_width++;
		for (int y = ADAM7_PASSES[p][1]; y < height; y += ADAM7_PASSES[p][3]) pass_height++;
		pass_sizes[p][0] = pass_width;
		pass_sizes[p][1] = pass_height;
		pass_scanline_bytes[p] = pass_width * bpp;
	}

	const unsigned char* ptr = data;
	unsigned char* curr_row = malloc(width * bpp);
	if (!curr_row) return 0;

	// For each pass
	for (int p = 0; p < 7; p++) {
		int pw = pass_sizes[p][0];
		int ph = pass_sizes[p][1];
		if (pw == 0 || ph == 0) continue; // skip empty passes

		// buffer for filtered pixels of pass
		unsigned char* filtered_pass = malloc(pw * ph * bpp);
		if (!filtered_pass) {
			free(curr_row);
			return 0;
		}

		// Decode filters for this pass (each scanline starts with a filter byte)
		const unsigned char* scanline_ptr = ptr;
		unsigned char* out_ptr = filtered_pass;
		unsigned char* prev_pass_row = NULL;

		for (int y = 0; y < ph; y++) {
			unsigned char filter = *scanline_ptr++;
			for (int i = 0; i < pw * bpp; i++) curr_row[i] = 0; // clear row buffer

			switch (filter) {
				case 0: // None
					memcpy(curr_row, scanline_ptr, pw * bpp);
					break;
				case 1: // Sub
					for (int i = 0; i < pw * bpp; i++) {
						unsigned char left = (i >= bpp) ? curr_row[i - bpp] : 0;
						curr_row[i] = scanline_ptr[i] + left;
					}
					break;
				case 2: // Up
					for (int i = 0; i < pw * bpp; i++) {
						unsigned char up = prev_pass_row ? prev_pass_row[i] : 0;
						curr_row[i] = scanline_ptr[i] + up;
					}
					break;
				case 3: // Average
					for (int i = 0; i < pw * bpp; i++) {
						unsigned char left = (i >= bpp) ? curr_row[i - bpp] : 0;
						unsigned char up = prev_pass_row ? prev_pass_row[i] : 0;
						curr_row[i] = scanline_ptr[i] + ((left + up) >> 1);
					}
					break;
				case 4: // Paeth
					for (int i = 0; i < pw * bpp; i++) {
						unsigned char left = (i >= bpp) ? curr_row[i - bpp] : 0;
						unsigned char up = prev_pass_row ? prev_pass_row[i] : 0;
						unsigned char up_left = (prev_pass_row && i >= bpp) ? prev_pass_row[i - bpp] : 0;
						curr_row[i] = scanline_ptr[i] + bkp_paeth_predictor(left, up, up_left);
					}
					break;
				default:
					free(filtered_pass);
					free(curr_row);
					return 0;
			}

			memcpy(out_ptr, curr_row, pw * bpp);
			prev_pass_row = out_ptr;
			out_ptr += pw * bpp;
			scanline_ptr += pw * bpp;
		}

		// Place pixels into final output buffer according to pass pattern
		int idx = 0;
		for (int y = ADAM7_PASSES[p][1]; y < height; y += ADAM7_PASSES[p][3]) {
			for (int x = ADAM7_PASSES[p][0]; x < width; x += ADAM7_PASSES[p][2]) {
				for (int c = 0; c < bpp; c++) {
					out[(y * width + x) * bpp + c] = filtered_pass[idx * bpp + c];
				}
				idx++;
			}
		}

		free(filtered_pass);
		ptr = scanline_ptr;
	}

	free(curr_row);
	return 1;
}

int bkp_read_gama(FILE* f, float* out_gamma, uint32_t length) {
	char type[5] = "gAMA";
	
	if (length != 4) return 0;
	
	unsigned char data[4];
	if (fread(data, 1, 4, f) != 4) return 0;

	uint32_t crc_read;
	if (!bkp_read_be32(f, &crc_read)) return 0;

	unsigned char crc_buf[8];
	memcpy(crc_buf, type, 4);
	memcpy(crc_buf + 4, data, 4);
	if (bkp_crc32(0, crc_buf, 8) != crc_read) return 0;

	uint32_t gamma_int = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
	*out_gamma = gamma_int / 100000.0f;
	return 1;
}

void bkp_apply_gamma_correction(unsigned char* pixels, uint32_t width, uint32_t height, float gamma) {
	if (gamma <= 0.0f) return; // invalid gamma
	float inv_gamma = 1.0f / gamma;

	for (uint32_t i = 0; i < width * height; i++) {
		for (int c = 0; c < 3; c++) { // RGB channels
			float normalized = pixels[i * 4 + c] / 255.0f;
			normalized = powf(normalized, inv_gamma);
			pixels[i * 4 + c] = (unsigned char)(normalized * 255.0f + 0.5f);
		}
	}
}

int bkp_skip_and_verify_chunk(FILE* f, uint32_t length, const char* type, bkp_buffer* idat_buf) {
	unsigned char* chunk_data = malloc(length);
	if (!chunk_data || fread(chunk_data, 1, length, f) != length) goto fail;

	uint32_t crc_read;
	if (!bkp_read_be32(f, &crc_read)) goto fail;

	unsigned char* crc_buf = malloc(length + 4);
	if (!crc_buf) goto fail;

	memcpy(crc_buf, type, 4);
	memcpy(crc_buf + 4, chunk_data, length);

	int ok = bkp_crc32(0, crc_buf, length + 4) == crc_read;
	free(crc_buf);
	free(chunk_data);
	return ok;

fail:
	if (chunk_data) free(chunk_data);
	if (idat_buf && idat_buf->data) {
		free(idat_buf->data);
		idat_buf->data = NULL;
	}
	return 0;
}

unsigned char* bkp_load_png(const char* path, uint32_t* out_width, uint32_t* out_height, int* out_color_type) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	
	unsigned char png_signature[8] = {137,80,78,71,13,10,26,10};
	unsigned char signature_read[8];

	if (fread(signature_read, 1, 8, f) != 8) {
		fclose(f);
		return NULL;
	}
	if (memcmp(png_signature, signature_read, 8) != 0) {
		fclose(f);
		return NULL;
	}

	bkp_ihdr ihdr = {0};
	if (!bkp_read_ihdr(f, &ihdr)) {
		fclose(f);
		return NULL;
	}

	bkp_palette palette = {0};
	bkp_buffer idat_buf = {0};

	int have_plte = 0;
	float gamma = 0.0f;

	// Read next chunk(s) until IDAT/IEND
	while (1) {
		uint32_t length;
		char type[5] = {0};
		if (!bkp_read_chunk_header(f, &length, type)) break;

		if (strcmp(type, "PLTE") == 0) {
			if (!bkp_read_plte(f, length, &palette)) break;
			have_plte = 1;
		} else if (strcmp(type, "IDAT") == 0) {
			if (!bkp_collect_idat_data(f, &idat_buf, length)) break;
		} else if (strcmp(type, "IEND") == 0) {
			break;
		} else if (strcmp(type, "gAMA") == 0) {
			if (!bkp_read_gama(f, &gamma, length)) break;
		} else {
			if (!bkp_skip_and_verify_chunk(f, length, type, &idat_buf)) break;
		}
	}
	
	fclose(f);

	if (idat_buf.data == NULL) return NULL;

	size_t decompressed_size = 0;
	unsigned char* decompressed = bkp_decompress_zlib(idat_buf.data, idat_buf.size, &decompressed_size);
	free(idat_buf.data);
	if (!decompressed) return NULL;

	// Expected raw size: (width * bpp + 1 filter byte) * height
	int bpp;
	switch (ihdr.color_type) {
		case BK_PNG_GRAY: bpp = 1; break;
		case BK_PNG_GRAY_ALPHA: bpp = 2; break;
		case BK_PNG_RGB: bpp = 3; break;
		case BK_PNG_INDEXED: bpp = 1; break;
		case BK_PNG_RGBA: bpp = 4; break;
		default:
			free(decompressed);
			return NULL;
	}

	size_t expected_size = (bpp * ihdr.width + 1) * ihdr.height;
	if (decompressed_size < expected_size) {
		free(decompressed);
		return NULL;
	}

	unsigned char* raw_pixels = malloc(ihdr.width * ihdr.height * 4); // output RGBA
	if (!raw_pixels) {
		free(decompressed);
		return NULL;
	}

	unsigned char* filtered_pixels = malloc(ihdr.width * ihdr.height * bpp);
	if (!filtered_pixels) {
		free(decompressed);
		free(raw_pixels);
		return NULL;
	}
	
	int success = 0;
	if (ihdr.interlace_method == 0) {
	success = bkp_filter_decode(decompressed, ihdr.width, ihdr.height, bpp, filtered_pixels);
	} else if (ihdr.interlace_method == 1) {
		success = bkp_decode_adam7(decompressed, ihdr.width, ihdr.height, bpp, filtered_pixels);
	} else {
		success = 0;
	}
	
	if (!success) {
		free(decompressed);
		free(filtered_pixels);
		free(raw_pixels);
		return NULL;
	}

	free(decompressed);

	// Convert pixel data to RGBA output
	if (ihdr.color_type == BK_PNG_INDEXED) {
		if (!have_plte) {
			free(filtered_pixels);
			free(raw_pixels);
			return NULL;
		}
		bkp_expand_palette(filtered_pixels, ihdr.width * ihdr.height, &palette, raw_pixels);
	} else if (ihdr.color_type == BK_PNG_GRAY) {
		for (uint32_t i = 0; i < ihdr.width * ihdr.height; i++) {
			unsigned char v = filtered_pixels[i];
			raw_pixels[i*4+0] = v;
			raw_pixels[i*4+1] = v;
			raw_pixels[i*4+2] = v;
			raw_pixels[i*4+3] = 255;
		}
	} else if (ihdr.color_type == BK_PNG_GRAY_ALPHA) {
		for (uint32_t i = 0; i < ihdr.width * ihdr.height; i++) {
			raw_pixels[i*4+0] = filtered_pixels[i*2+0];
			raw_pixels[i*4+1] = filtered_pixels[i*2+0];
			raw_pixels[i*4+2] = filtered_pixels[i*2+0];
			raw_pixels[i*4+3] = filtered_pixels[i*2+1];
		}
	} else if (ihdr.color_type == BK_PNG_RGB) {
		for (uint32_t i = 0; i < ihdr.width * ihdr.height; i++) {
			raw_pixels[i*4+0] = filtered_pixels[i*3+0];
			raw_pixels[i*4+1] = filtered_pixels[i*3+1];
			raw_pixels[i*4+2] = filtered_pixels[i*3+2];
			raw_pixels[i*4+3] = 255;
		}
	} else if (ihdr.color_type == BK_PNG_RGBA) {
		memcpy(raw_pixels, filtered_pixels, ihdr.width * ihdr.height * 4);
	} else {
		free(filtered_pixels);
		free(raw_pixels);
		return NULL;
	}

	free(filtered_pixels);

	if (out_width) *out_width = ihdr.width;
	if (out_height) *out_height = ihdr.height;
	if (out_color_type) *out_color_type = ihdr.color_type;
	
	if (gamma > 0.0f) {
		bkp_apply_gamma_correction(raw_pixels, ihdr.width, ihdr.height, gamma);
	}

	return raw_pixels;
}

#endif