/***************************************************************************
 * ROM Properties Page shell extension. (librptexture)                     *
 * PowerVR3.cpp: PowerVR 3.0.0 texture image reader.                       *
 *                                                                         *
 * Copyright (c) 2019 by David Korth.                                      *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

/**
 * References:
 * - http://cdn.imgtec.com/sdk-documentation/PVR+File+Format.Specification.pdf
 */

#include "PowerVR3.hpp"
#include "FileFormat_p.hpp"

#include "pvr3_structs.h"

// librpbase
#include "librpbase/common.h"
#include "librpbase/byteswap.h"
#include "librpbase/aligned_malloc.h"
#include "librpbase/RomFields.hpp"
#include "librpbase/file/IRpFile.hpp"
using namespace LibRpBase;

// librptexture
#include "img/rp_image.hpp"
#include "decoder/ImageDecoder.hpp"

// C includes. (C++ namespace)
#include "librpbase/ctypex.h"
#include <cassert>
#include <cerrno>
#include <cstring>

// C++ includes.
#include <memory>
using std::unique_ptr;

namespace LibRpTexture {

FILEFORMAT_IMPL(PowerVR3)

class PowerVR3Private : public FileFormatPrivate
{
	public:
		PowerVR3Private(PowerVR3 *q, IRpFile *file);
		~PowerVR3Private();

	private:
		typedef FileFormatPrivate super;
		RP_DISABLE_COPY(PowerVR3Private)

	public:
		// PVR3 header.
		PowerVR3_Header pvr3Header;

		// Decoded image.
		rp_image *img;

		// Invalid pixel format message.
		char invalid_pixel_format[40];

		// Is byteswapping needed?
		// (PVR3 file has the opposite endianness.)
		bool isByteswapNeeded;

		// Is HFlip/VFlip needed?
		// Some textures may be stored upside-down due to
		// the way GL texture coordinates are interpreted.
		// Default without orientation metadata is HFlip=false, VFlip=false
		uint8_t isFlipNeeded;
		enum FlipBits : uint8_t {
			FLIP_NONE	= 0,
			FLIP_V		= (1 << 0),
			FLIP_H		= (1 << 1),
			FLIP_HV		= FLIP_H | FLIP_V,
		};

		// Metadata.
		bool orientation_valid;
		PowerVR3_Metadata_Orientation orientation;

		// Texture data start address.
		unsigned int texDataStartAddr;

		/**
		 * Uncompressed format lookup table.
		 * NOTE: pixel_format appears byteswapped here because trailing '\0'
		 * isn't supported by MSVC, so e.g. 'rgba' is 'abgr', and
		 * 'i\0\0\0' is '\0\0\0i'. This *does* match the LE format, though.
		 * Channel depth uses the logical format, e.g. 0x00000008 or 0x00080808.
		 */
		struct FmtLkup_t {
			uint32_t pixel_format;
			uint32_t channel_depth;
			uint8_t pxfmt;
			uint8_t bits;	// 8, 15, 16, 24, 32
		};
		static const struct FmtLkup_t fmtLkup_tbl[];

		/**
		 * Load the image.
		 * @param mip Mipmap number. (0 == full image)
		 * @return Image, or nullptr on error.
		 */
		const rp_image *loadImage(int mip);

		/**
		 * Load PowerVR3 metadata.
		 * @return 0 on success; negative POSIX error code on error.
		 */
		int loadPvr3Metadata(void);
};

/** PowerVR3Private **/

/**
 * Uncompressed format lookup table.
 * NOTE: pixel_format appears byteswapped here because trailing '\0'
 * isn't supported by MSVC, so e.g. 'rgba' is 'abgr', and
 * 'i\0\0\0' is '\0\0\0i'. This *does* match the LE format, though.
 * Channel depth uses the logical format, e.g. 0x00000008 or 0x00080808.
 */
const struct PowerVR3Private::FmtLkup_t PowerVR3Private::fmtLkup_tbl[] = {
	//{'\0\0\0i', 0x00000008, ImageDecoder::PXF_I8,		8},
	//{'\0\0\0r', 0x00000008, ImageDecoder::PXF_R8,		8},
	{'\0\0\0a', 0x00000008, ImageDecoder::PXF_A8,		8},
	{ '\0\0gr', 0x00000808, ImageDecoder::PXF_GR88,		16},
	{  '\0bgr', 0x00080808, ImageDecoder::PXF_BGR888,	24},
	{   'abgr', 0x08080808, ImageDecoder::PXF_ABGR8888,	32},
	{   'rgba', 0x08080808, ImageDecoder::PXF_RGBA8888,	32},
	//{'\0\0\0r', 0x00000010, ImageDecoder::PXF_R16,		16},
	{ '\0\0gr', 0x00001010, ImageDecoder::PXF_G16R16,	32},
	//{'\0\0\0r', 0x00000020, ImageDecoder::PXF_R32,		32},
	//{ '\0\0gr', 0x00002020, ImageDecoder::PXF_G32R32,	32},
	//{  '\0bgr', 0x00202020, ImageDecoder::PXF_B32G32R32,	32},
	//{   'abgr', 0x20202020, ImageDecoder::PXF_A32B32G32R32,	32},
	{  '\0bgr', 0x00050605, ImageDecoder::PXF_BGR565,	16},
	{   'abgr', 0x04040404, ImageDecoder::PXF_ABGR4444,	16},
	{   'abgr', 0x01050505, ImageDecoder::PXF_ABGR1555,	16},
	{  '\0rgb', 0x00080808, ImageDecoder::PXF_RGB888,	24},
	{   'argb', 0x08080808, ImageDecoder::PXF_ARGB8888,	32},
#if 0
	// TODO: Depth/stencil formats.
	{'\0\0\0d', 0x00000008},
	{'\0\0\0d', 0x00000010},
	{'\0\0\0d', 0x00000018},
	{'\0\0\0d', 0x00000020},
	{ '\0\0sd', 0x00000810},
	{ '\0\0sd', 0x00000818},
	{ '\0\0sd', 0x00000820},
	{'\0\0\0s', 0x00000008},
#endif
#if 0
	// TODO: High-bit-depth luminance.
	{'\0\0\0l', 0x00000020, ImageDecoder::PXF_L32,		32},
	{ '\0\0al', 0x00001010, ImageDecoder::PXF_A16L16,	32},
	{ '\0\0al', 0x00002020, ImageDecoder::PXF_A32L32,	32},
#endif
#if 0
	// TODO: "Weird" formats.
	{   'abgr', 0x10101010, ImageDecoder::PXF_A16B16G16R16,	64},
	{  '\0bgr', 0x00101010, ImageDecoder::PXF_B16G16R16,	48},
	{  '\0rgb', 0x000B0B0A, ImageDecoder::PXF_R11G11B10,	32},
#endif

	{0, 0, 0, 0}
};

PowerVR3Private::PowerVR3Private(PowerVR3 *q, IRpFile *file)
	: super(q, file)
	, img(nullptr)
	, isByteswapNeeded(false)
	, isFlipNeeded(FLIP_NONE)
	, orientation_valid(false)
	, texDataStartAddr(0)
{
	// Clear the PowerVR3 header struct.
	memset(&pvr3Header, 0, sizeof(pvr3Header));
	memset(&orientation, 0, sizeof(orientation));
	memset(invalid_pixel_format, 0, sizeof(invalid_pixel_format));
}

PowerVR3Private::~PowerVR3Private()
{
	delete img;
}

/**
 * Load the image.
 * @param mip Mipmap number. (0 == full image)
 * @return Image, or nullptr on error.
 */
const rp_image *PowerVR3Private::loadImage(int mip)
{
	if (img) {
		// Image has already been loaded.
		return img;
	} else if (!this->file || !this->isValid) {
		// Can't load the image.
		return nullptr;
	}

	// TODO: Support >1 surface and face? (read the first one)
	if (pvr3Header.num_surfaces != 1 || pvr3Header.num_faces != 1) {
		// Not supported.
		return nullptr;
	}

	// Sanity check: Maximum image dimensions of 32768x32768.
	// NOTE: `height == 0` is allowed here. (1D texture)
	assert(pvr3Header.width > 0);
	assert(pvr3Header.width <= 32768);
	assert(pvr3Header.height <= 32768);
	if (pvr3Header.width == 0 || pvr3Header.width > 32768 ||
	    pvr3Header.height > 32768)
	{
		// Invalid image dimensions.
		return nullptr;
	}

	// Texture cannot start inside of the PowerVR3 header.
	assert(texDataStartAddr >= sizeof(pvr3Header));
	if (texDataStartAddr < sizeof(pvr3Header)) {
		// Invalid texture data start address.
		return nullptr;
	}

	if (file->size() > 128*1024*1024) {
		// Sanity check: PowerVR3 files shouldn't be more than 128 MB.
		return nullptr;
	}
	const uint32_t file_sz = (uint32_t)file->size();

	// Seek to the start of the texture data.
	int ret = file->seek(texDataStartAddr);
	if (ret != 0) {
		// Seek error.
		return nullptr;
	}

	// NOTE: Mipmaps are stored *after* the main image.
	// Hence, no mipmap processing is necessary.

	// TODO: Support mipmaps.
	if (mip != 0) {
		return nullptr;
	}

	// Handle a 1D texture as a "width x 1" 2D texture.
	// NOTE: Handling a 3D texture as a single 2D texture.
	const int height = (pvr3Header.height > 0 ? pvr3Header.height : 1);

	// Calculate the expected size.
	uint32_t expected_size;
	const FmtLkup_t *fmtLkup = nullptr;
	if (pvr3Header.channel_depth != 0) {
		// Uncompressed format.
		// Find a supported format that matches.
		for (const FmtLkup_t *p = fmtLkup_tbl; p->pixel_format != 0; p++) {
			if (p->pixel_format == pvr3Header.pixel_format &&
			    p->channel_depth == pvr3Header.channel_depth)
			{
				fmtLkup = p;
				break;
			}
		}
		if (!fmtLkup) {
			// Not found.
			return nullptr;
		}

		// Convert to bytes, rounding up.
		const unsigned int bytes = ((fmtLkup->bits + 7) & ~7) / 8;

		// TODO: Minimum row width?
		// TODO: Does 'rgb' use 24-bit or 32-bit?
		expected_size = pvr3Header.width * height * bytes;
	} else {
		// Compressed format.
		// TODO
		return nullptr;
	}

	// Verify file size.
	if ((texDataStartAddr + expected_size) > file_sz) {
		// File is too small.
		return nullptr;
	}

	// Read the texture data.
	auto buf = aligned_uptr<uint8_t>(16, expected_size);
	size_t size = file->seekAndRead(texDataStartAddr, buf.get(), expected_size);
	if (size != expected_size) {
		// Seek and/or read error.
		return nullptr;
	}

	// Decode the image.
	if (pvr3Header.channel_depth != 0) {
		// Uncompressed format.
		assert(fmtLkup != nullptr);
		if (!fmtLkup) {
			// Shouldn't happen...
			return nullptr;
		}

		// TODO: Is the row stride required to be a specific multiple?
		switch (fmtLkup->bits) {
			case 8:
				// 8-bit
				img = ImageDecoder::fromLinear8(
					static_cast<ImageDecoder::PixelFormat>(fmtLkup->pxfmt),
					pvr3Header.width, height,
					buf.get(), expected_size);
				break;

			case 15:
			case 16:
				// 15/16-bit
				img = ImageDecoder::fromLinear16(
					static_cast<ImageDecoder::PixelFormat>(fmtLkup->pxfmt),
					pvr3Header.width, height,
					reinterpret_cast<const uint16_t*>(buf.get()), expected_size);
				break;

			case 24:
				// 24-bit
				img = ImageDecoder::fromLinear24(
					static_cast<ImageDecoder::PixelFormat>(fmtLkup->pxfmt),
					pvr3Header.width, height,
					buf.get(), expected_size);
				break;

			case 32:
				// 32-bit
				img = ImageDecoder::fromLinear32(
					static_cast<ImageDecoder::PixelFormat>(fmtLkup->pxfmt),
					pvr3Header.width, height,
					reinterpret_cast<const uint32_t*>(buf.get()), expected_size);
				break;

			default:
				// Not supported...
				assert(!"Unsupported PowerVR3 uncompressed format.");
				return nullptr;
		}
	} else {
		// Compressed format.
		// TODO
		return nullptr;
	}

	// Post-processing: Check if VFlip is needed.
	// TODO: Handle HFlip too?
	if (img && (isFlipNeeded & FLIP_V) && height > 1) {
		// TODO: Assert that img dimensions match ktxHeader?
		rp_image *flipimg = img->vflip();
		if (flipimg) {
			// Swap the images.
			std::swap(img, flipimg);
			// Delete the original image.
			delete flipimg;
		}
	}

	return img;
}

/**
 * Load PowerVR3 metadata.
 * @return 0 on success; negative POSIX error code on error.
 */
int PowerVR3Private::loadPvr3Metadata(void)
{
	if (pvr3Header.metadata_size == 0) {
		// No metadata.
		return 0;
	} else if (pvr3Header.metadata_size <= sizeof(PowerVR3_Metadata_Block_Header_t)) {
		// Metadata is present, but it's too small...
		return -EIO;
	}

	// Sanity check: Metadata shouldn't be more than 128 KB.
	assert(pvr3Header.metadata_size <= 128*1024);
	if (pvr3Header.metadata_size > 128*1024) {
		return -ENOMEM;
	}

	// Parse the additional metadata.
	int ret = 0;
	unique_ptr<uint8_t[]> buf(new uint8_t[pvr3Header.metadata_size]);
	size_t size = file->seekAndRead(sizeof(pvr3Header), buf.get(), pvr3Header.metadata_size);
	if (size != pvr3Header.metadata_size) {
		return -EIO;
	}

	uint8_t *p = buf.get();
	uint8_t *const p_end = p + pvr3Header.metadata_size;
	// FIXME: Might overflow...
	while (p + sizeof(PowerVR3_Metadata_Block_Header_t) < p_end) {
		PowerVR3_Metadata_Block_Header_t *const pHdr =
			reinterpret_cast<PowerVR3_Metadata_Block_Header_t*>(p);
		p += sizeof(*pHdr);

		// Byteswap the header, if necessary.
		if (isByteswapNeeded) {
			pHdr->fourCC = __swab32(pHdr->fourCC);
			pHdr->key    = __swab32(pHdr->key);
			pHdr->size   = __swab32(pHdr->size);
		}

		// Check the fourCC.
		if (pHdr->fourCC != PVR3_VERSION_HOST) {
			// Not supported.
			p += pHdr->size;
			continue;
		}

		// Check the key.
		switch (pHdr->key) {
			case PVR3_META_ORIENTATION: {
				// Logical orientation.
				if (p + sizeof(orientation) > p_end) {
					// Out of bounds...
					p = p_end;
					break;
				}

				// Copy the orientation bytes.
				memcpy(&orientation, p, sizeof(orientation));
				orientation_valid = true;
				p += sizeof(orientation);

				// Set the flip bits.
				// TODO: Z flip?
				isFlipNeeded = 0;
				if (orientation.x != 0) {
					isFlipNeeded |= FLIP_H;
				}
				if (orientation.y != 0) {
					isFlipNeeded |= FLIP_V;
				}
				break;
			}

			default:
			case PVR3_META_TEXTURE_ATLAS:
			case PVR3_META_NORMAL_MAP:
			case PVR3_META_CUBE_MAP:
			case PVR3_META_BORDER:
			case PVR3_META_PADDING:
				// TODO: Not supported.
				p += pHdr->size;
				break;
		}
	}

	// Metadata parsed.
	return ret;
}

/** PowerVR3 **/

/**
 * Read a PowerVR 3.0.0 texture image file.
 *
 * A ROM image must be opened by the caller. The file handle
 * will be ref()'d and must be kept open in order to load
 * data from the ROM image.
 *
 * To close the file, either delete this object or call close().
 *
 * NOTE: Check isValid() to determine if this is a valid ROM.
 *
 * @param file Open ROM image.
 */
PowerVR3::PowerVR3(IRpFile *file)
	: super(new PowerVR3Private(this, file))
{
	RP_D(PowerVR3);

	if (!d->file) {
		// Could not ref() the file handle.
		return;
	}

	// Read the PowerVR3 header.
	d->file->rewind();
	size_t size = d->file->read(&d->pvr3Header, sizeof(d->pvr3Header));
	if (size != sizeof(d->pvr3Header)) {
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Verify the PVR3 magic/version.
	if (d->pvr3Header.version == PVR3_VERSION_HOST) {
		// Host-endian. Byteswapping is not needed.
		d->isByteswapNeeded = false;
	} else if (d->pvr3Header.version == PVR3_VERSION_SWAP) {
		// Swap-endian. Byteswapping is needed.
		// NOTE: Keeping `version` unswapped in case
		// the actual image data needs to be byteswapped.
		d->pvr3Header.flags		= __swab32(d->pvr3Header.flags);

		// Pixel format is technically 64-bit, so we have to
		// byteswap *and* swap both DWORDs.
		const uint32_t channel_depth	= __swab32(d->pvr3Header.pixel_format);
		const uint32_t pixel_format	= __swab32(d->pvr3Header.channel_depth);
		d->pvr3Header.pixel_format	= pixel_format;
		d->pvr3Header.channel_depth	= channel_depth;

		d->pvr3Header.color_space	= __swab32(d->pvr3Header.color_space);
		d->pvr3Header.channel_type	= __swab32(d->pvr3Header.channel_type);
		d->pvr3Header.height		= __swab32(d->pvr3Header.height);
		d->pvr3Header.width		= __swab32(d->pvr3Header.width);
		d->pvr3Header.depth		= __swab32(d->pvr3Header.depth);
		d->pvr3Header.num_surfaces	= __swab32(d->pvr3Header.num_surfaces);
		d->pvr3Header.num_faces		= __swab32(d->pvr3Header.num_faces);
		d->pvr3Header.mipmap_count	= __swab32(d->pvr3Header.mipmap_count);
		d->pvr3Header.metadata_size	= __swab32(d->pvr3Header.metadata_size);

		// Convenience flag.
		d->isByteswapNeeded = true;
	} else {
		// Invalid magic.
		d->isValid = false;
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// File is valid.
	d->isValid = true;

	// Texture data start address.
	d->texDataStartAddr = sizeof(d->pvr3Header) + d->pvr3Header.metadata_size;

	// Load PowerVR metadata.
	// This function checks for the orientation block
	// and sets the HFlip/VFlip values as necessary.
	d->loadPvr3Metadata();

	// Cache the dimensions for the FileFormat base class.
	d->dimensions[0] = d->pvr3Header.width;
	d->dimensions[1] = d->pvr3Header.height;
	d->dimensions[2] = d->pvr3Header.depth;
}

/** Class-specific functions that can be used even if isValid() is false. **/

/**
 * Get a list of all supported file extensions.
 * This is to be used for file type registration;
 * subclasses don't explicitly check the extension.
 *
 * NOTE: The extensions include the leading dot,
 * e.g. ".bin" instead of "bin".
 *
 * NOTE 2: The array and the strings in the array should
 * *not* be freed by the caller.
 *
 * @return NULL-terminated array of all supported file extensions, or nullptr on error.
 */
const char *const *PowerVR3::supportedFileExtensions_static(void)
{
	static const char *const exts[] = {
		".pvr",		// NOTE: Same as SegaPVR.
		nullptr
	};
	return exts;
}

/**
 * Get a list of all supported MIME types.
 * This is to be used for metadata extractors that
 * must indicate which MIME types they support.
 *
 * NOTE: The array and the strings in the array should
 * *not* be freed by the caller.
 *
 * @return NULL-terminated array of all supported file extensions, or nullptr on error.
 */
const char *const *PowerVR3::supportedMimeTypes_static(void)
{
	static const char *const mimeTypes[] = {
		// Unofficial MIME types.
		// TODO: Get these upstreamed on FreeDesktop.org.
		"image/x-pvr",

		nullptr
	};
	return mimeTypes;
}

/** Property accessors **/

/**
 * Get the texture format name.
 * @return Texture format name, or nullptr on error.
 */
const char *PowerVR3::textureFormatName(void) const
{
	RP_D(const PowerVR3);
	if (!d->isValid)
		return nullptr;

	return "PowerVR";
}

/**
 * Get the pixel format, e.g. "RGB888" or "DXT1".
 * @return Pixel format, or nullptr if unavailable.
 */
const char *PowerVR3::pixelFormat(void) const
{
	// TODO: Localization.
#define C_(ctx, str) str
#define NOP_C_(ctx, str) str

	RP_D(const PowerVR3);
	if (!d->isValid)
		return nullptr;

	if (d->invalid_pixel_format[0] != '\0') {
		return d->invalid_pixel_format;
	}

	// TODO: Localization?
	if (d->pvr3Header.channel_depth == 0) {
		// Compressed texture format.
		static const char *const pvr3PxFmt_tbl[] = {
			// 0
			"PVRTC 2bpp RGB", "PVRTC 2bpp RGBA",
			"PVRTC 4bpp RGB", "PVRTC 4bpp RGBA",
			"PVRTC-II 2bpp", "PVRTC-II 4bpp",
			"ETC1", "DXT1", "DXT2", "DXT3", "DXT4", "DXT5",
			"BC4", "BC5", "BC6", "BC7",

			// 16
			"UYVY", "YUY2", "BW1bpp", "R9G9B9E5 Shared Exponent",
			"RGBG8888", "GRGB8888", "ETC2 RGB", "ETC2 RGBA",
			"ETC2 RGB A1", "EAC R11", "EAC RG11",

			// 27
			"ASTC_4x4", "ASTC_5x4", "ASTC_5x5", "ASTC_6x5", "ATC_6x6",

			// 32
			"ASTC_8x5", "ASTC_8x6", "ASTC_8x8", "ASTC_10x5",
			"ASTC_10x6", "ASTC_10x8", "ASTC_10x10", "ASTC_12x10",
			"ASTC_12x12",

			// 41
			"ASTC_3x3x3", "ASTC_4x3x3", "ASTC_4x4x3", "ASTC_4x4x4",
			"ASTC_5x4x4", "ASTC_5x5x4", "ASTC_5x5x5", "ASTC_6x5x5",
			"ASTC_6x6x5", "ASTC_6x6x6",
		};
		static_assert(ARRAY_SIZE(pvr3PxFmt_tbl) == PVR3_PXF_MAX, "pvr3PxFmt_tbl[] needs to be updated!");

		if (d->pvr3Header.pixel_format < ARRAY_SIZE(pvr3PxFmt_tbl)) {
			return pvr3PxFmt_tbl[d->pvr3Header.pixel_format];
		}

		// Not valid.
		snprintf(const_cast<PowerVR3Private*>(d)->invalid_pixel_format,
			sizeof(d->invalid_pixel_format),
			"Unknown (Compressed: 0x%08X)", d->pvr3Header.pixel_format);
		return d->invalid_pixel_format;
	}

	// Uncompressed pixel formats.
	// These are literal channel identifiers, e.g. 'rgba',
	// followed by a color depth value for each channel.

	// NOTE: Pixel formats are stored in literal order in
	// little-endian files, so the low byte is the first channel.
	// TODO: Verify big-endian.

	char s_pxf[8], s_chcnt[16];
	char *p_pxf = s_pxf;
	char *p_chcnt = s_chcnt;

	uint32_t pixel_format = d->pvr3Header.pixel_format;
	uint32_t channel_depth = d->pvr3Header.channel_depth;
	for (unsigned int i = 0; i < 4; i++, pixel_format >>= 8, channel_depth >>= 8) {
		uint8_t pxf = (pixel_format & 0xFF);
		if (pxf == 0)
			break;

		*p_pxf++ = TOUPPER(pxf);
		p_chcnt += sprintf(p_chcnt, "%u", channel_depth & 0xFF);
	}
	*p_pxf = '\0';
	*p_chcnt = '\0';

	if (s_pxf[0] != '\0') {
		snprintf(const_cast<PowerVR3Private*>(d)->invalid_pixel_format,
			 sizeof(d->invalid_pixel_format),
			 "%s%s", s_pxf, s_chcnt);
	} else {
		strcpy(const_cast<PowerVR3Private*>(d)->invalid_pixel_format, C_("RomData", "Unknown"));
	}
	return d->invalid_pixel_format;
}

/**
 * Get the mipmap count.
 * @return Number of mipmaps. (0 if none; -1 if format doesn't support mipmaps)
 */
int PowerVR3::mipmapCount(void) const
{
	RP_D(const PowerVR3);
	if (!d->isValid)
		return -1;

	// Mipmap count.
	return d->pvr3Header.mipmap_count;
}

#ifdef ENABLE_LIBRPBASE_ROMFIELDS
/**
 * Get property fields for rom-properties.
 * @param fields RomFields object to which fields should be added.
 * @return Number of fields added, or 0 on error.
 */
int PowerVR3::getFields(LibRpBase::RomFields *fields) const
{
	// TODO: Localization.
#define C_(ctx, str) str
#define NOP_C_(ctx, str) str

	assert(fields != nullptr);
	if (!fields)
		return 0;

	RP_D(PowerVR3);
	if (!d->isValid) {
		// Unknown file type.
		return -EIO;
	}

	const PowerVR3_Header *const pvr3Header = &d->pvr3Header;
	const int initial_count = fields->count();
	fields->reserve(initial_count + 7);	// Maximum of 7 fields. (TODO)

	// TODO: Handle PVR 1.0 and 2.0 headers.
	fields->addField_string(C_("PowerVR3", "Version"), "3.0.0");

	// Endianness.
	// TODO: Save big vs. little in the constructor instead of just "needs byteswapping"?
	const char *endian_str;
	if (pvr3Header->version == PVR3_VERSION_HOST) {
		// Matches host-endian.
#if SYS_BYTEORDER == SYS_LIL_ENDIAN
		endian_str = C_("PowerVR3", "Little-Endian");
#else /* SYS_BYTEORDER == SYS_BIG_ENDIAN */
		endian_str = C_("PowerVR3", "Big-Endian");
#endif
	} else {
		// Does not match host-endian.
#if SYS_BYTEORDER == SYS_LIL_ENDIAN
		endian_str = C_("PowerVR3", "Big-Endian");
#else /* SYS_BYTEORDER == SYS_BIG_ENDIAN */
		endian_str = C_("PowerVR3", "Little-Endian");
#endif
	}
	fields->addField_string(C_("PowerVR3", "Endianness"), endian_str);

	// Color space.
	static const char *const pvr3_colorspace_tbl[] = {
		NOP_C_("PowerVR3|ColorSpace", "Linear RGB"),
		NOP_C_("PowerVR3|ColorSpace", "sRGB"),
	};
	static_assert(ARRAY_SIZE(pvr3_colorspace_tbl) == PVR3_COLOR_SPACE_MAX, "pvr3_colorspace_tbl[] needs to be updated!");
	if (pvr3Header->color_space < ARRAY_SIZE(pvr3_colorspace_tbl)) {
		fields->addField_string(C_("PowerVR3", "Color Space"),
			pvr3_colorspace_tbl[pvr3Header->color_space]);
			//dpgettext_expr(RP_I18N_DOMAIN, "PowerVR3|ColorSpace", pvr3_colorspace[pvr3Header->color_space]));
	} else {
		fields->addField_string_numeric(C_("PowerVR3", "Color Space"),
			pvr3Header->color_space);
	}

	// Channel type.
	static const char *const pvr3_chtype_tbl[] = {
		NOP_C_("PowerVR3|ChannelType", "Unsigned Byte (normalized)"),
		NOP_C_("PowerVR3|ChannelType", "Signed Byte (normalized)"),
		NOP_C_("PowerVR3|ChannelType", "Unsigned Byte"),
		NOP_C_("PowerVR3|ChannelType", "Signed Byte"),
		NOP_C_("PowerVR3|ChannelType", "Unsigned Short (normalized)"),
		NOP_C_("PowerVR3|ChannelType", "Signed Short (normalized)"),
		NOP_C_("PowerVR3|ChannelType", "Unsigned Short"),
		NOP_C_("PowerVR3|ChannelType", "Signed Short"),
		NOP_C_("PowerVR3|ChannelType", "Unsigned Integer (normalized)"),
		NOP_C_("PowerVR3|ChannelType", "Signed Integer (normalized)"),
		NOP_C_("PowerVR3|ChannelType", "Unsigned Integer"),
		NOP_C_("PowerVR3|ChannelType", "Signed Integer"),
		NOP_C_("PowerVR3|ChannelType", "Float"),
	};
	static_assert(ARRAY_SIZE(pvr3_chtype_tbl) == PVR3_CHTYPE_MAX, "pvr3_chtype_tbl[] needs to be updated!");
	if (pvr3Header->channel_type < ARRAY_SIZE(pvr3_chtype_tbl)) {
		fields->addField_string(C_("PowerVR3", "Channel Type"),
			pvr3_chtype_tbl[pvr3Header->channel_type]);
			//dpgettext_expr(RP_I18N_DOMAIN, "PowerVR3|ChannelType", pvr3_chtype_tbl[pvr3Header->channel_type]));
	} else {
		fields->addField_string_numeric(C_("PowerVR3", "Channel Type"),
			pvr3Header->channel_type);
	}

	// Other numeric fields.
	fields->addField_string_numeric(C_("PowerVR3", "# of Surfaces"),
		pvr3Header->num_surfaces);
	fields->addField_string_numeric(C_("PowerVR3", "# of Faces"),
		pvr3Header->num_faces);

	// Orientation.
	if (d->orientation_valid) {
		// Using KTX-style formatting.
		// TODO: Is 1D set using height or width?
		char str[16];
		if (pvr3Header->depth > 1) {
			snprintf(str, sizeof(str), "S=%c,T=%c,R=%c",
				(d->orientation.x != 0 ? 'l' : 'r'),
				(d->orientation.y != 0 ? 'u' : 'd'),
				(d->orientation.z != 0 ? 'o' : 'i'));
		} else if (pvr3Header->height > 1) {
			snprintf(str, sizeof(str), "S=%c,T=%c",
				(d->orientation.x != 0 ? 'l' : 'r'),
				(d->orientation.y != 0 ? 'u' : 'd'));
		} else {
			snprintf(str, sizeof(str), "S=%c",
				(d->orientation.x != 0 ? 'l' : 'r'));
		}
		fields->addField_string(C_("PowerVR3", "Orientation"), str);
	}

	// TODO: Additional fields.

	// Finished reading the field data.
	return (fields->count() - initial_count);
}
#endif /* ENABLE_LIBRPBASE_ROMFIELDS */

/** Image accessors **/

/**
 * Get the image.
 * For textures with mipmaps, this is the largest mipmap.
 * The image is owned by this object.
 * @return Image, or nullptr on error.
 */
const rp_image *PowerVR3::image(void) const
{
	// The full image is mipmap 0.
	return this->mipmap(0);
}

/**
 * Get the image for the specified mipmap.
 * Mipmap 0 is the largest image.
 * @param mip Mipmap number.
 * @return Image, or nullptr on error.
 */
const rp_image *PowerVR3::mipmap(int mip) const
{
	RP_D(const PowerVR3);
	if (!d->isValid) {
		// Unknown file type.
		return nullptr;
	}

	// Load the image.
	return const_cast<PowerVR3Private*>(d)->loadImage(mip);
}

}
