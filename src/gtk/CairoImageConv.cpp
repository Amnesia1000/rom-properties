/***************************************************************************
 * ROM Properties Page shell extension. (GTK+ common)                      *
 * CairoImageConv.cpp: Helper functions to convert from rp_image to Cairo. *
 *                                                                         *
 * Copyright (c) 2017-2018 by David Korth.                                 *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify it *
 * under the terms of the GNU General Public License as published by the   *
 * Free Software Foundation; either version 2 of the License, or (at your  *
 * option) any later version.                                              *
 *                                                                         *
 * This program is distributed in the hope that it will be useful, but     *
 * WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License       *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 ***************************************************************************/

#include "CairoImageConv.hpp"

// C includes.
#include <stdint.h>

// C includes. (C++ namespace)
#include <cassert>
#include <cstring>

// librpbase
#include "librpbase/img/rp_image.hpp"
using LibRpBase::rp_image;

/**
 * Convert an rp_image to cairo_surface_t.
 * @param img	[in] rp_image.
 * @return GdkPixbuf, or nullptr on error.
 */
cairo_surface_t *CairoImageConv::rp_image_to_cairo_surface_t(const rp_image *img)
{
	assert(img != nullptr);
	if (unlikely(!img || !img->isValid()))
		return nullptr;

	// NOTE: cairo_image_surface_create_for_data() doesn't do a
	// deep copy, so we can't use it.
	// NOTE 2: cairo_image_surface_create() always returns a valid
	// pointer, but the status may be CAIRO_STATUS_NULL_POINTER if
	// it failed to create a surface. We'll still check for nullptr.
	const int width = img->width();
	const int height = img->height();
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	assert(surface != nullptr);
	assert(cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
	if (unlikely(!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)) {
		return nullptr;
	}

	uint32_t *px_dest = reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(surface));
	assert(px_dest != nullptr);

	switch (img->format()) {
		case rp_image::FORMAT_ARGB32: {
			// Copy the image data.
			const uint32_t *img_buf = static_cast<const uint32_t*>(img->bits());
			const int dest_stride = cairo_image_surface_get_stride(surface) / sizeof(uint32_t);
			const int src_stride = img->stride() / sizeof(uint32_t);
			const int row_bytes = img->row_bytes();

			for (unsigned int y = (unsigned int)height; y > 0; y--) {
				memcpy(px_dest, img_buf, row_bytes);
				px_dest += dest_stride;
				img_buf += src_stride;
			}

			// Mark the surface as dirty.
			cairo_surface_mark_dirty(surface);
			break;
		}

		case rp_image::FORMAT_CI8: {
			const uint32_t *palette = img->palette();
			const int palette_len = img->palette_len();
			assert(palette != nullptr);
			assert(palette_len > 0);
			if (!palette || palette_len <= 0)
				break;

			// FIXME: Verify that the palette is 256 colors.

			// Copy the image data.
			const uint8_t *img_buf = static_cast<const uint8_t*>(img->bits());
			const int dest_stride_adj = (cairo_image_surface_get_stride(surface) / sizeof(uint32_t)) - width;
			const int src_stride_adj = img->stride() - width;

			for (unsigned int y = (unsigned int)height; y > 0; y--) {
				unsigned int x;
				for (x = (unsigned int)width; x > 3; x -= 4) {
					px_dest[0] = palette[img_buf[0]];
					px_dest[1] = palette[img_buf[1]];
					px_dest[2] = palette[img_buf[2]];
					px_dest[3] = palette[img_buf[3]];
					px_dest += 4;
					img_buf += 4;
				}
				for (; x > 0; x--, px_dest++, img_buf++) {
					// Last pixels.
					*px_dest = palette[*img_buf];
					px_dest++;
					img_buf++;
				}

				// Next line.
				img_buf += src_stride_adj;
				px_dest += dest_stride_adj;
			}

			// Mark the surface as dirty.
			cairo_surface_mark_dirty(surface);
			break;
		}

		default:
			// Unsupported image format.
			assert(!"Unsupported rp_image::Format.");
			cairo_surface_destroy(surface);
			surface = nullptr;
			break;
	}

	return surface;
}
