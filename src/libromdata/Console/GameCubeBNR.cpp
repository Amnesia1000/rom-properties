/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * GameCubeBNR.cpp: Nintendo GameCube banner reader.                       *
 *                                                                         *
 * Copyright (c) 2016-2018 by David Korth.                                 *
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

#include "GameCubeBNR.hpp"
#include "librpbase/RomData_p.hpp"

#include "gcn_banner.h"
#include "data/NintendoLanguage.hpp"

// librpbase
#include "librpbase/common.h"
#include "librpbase/byteswap.h"
#include "librpbase/aligned_malloc.h"
#include "librpbase/TextFuncs.hpp"
#include "librpbase/file/IRpFile.hpp"

#include "librpbase/img/rp_image.hpp"
#include "librpbase/img/ImageDecoder.hpp"

#include "libi18n/i18n.h"
using namespace LibRpBase;

// C includes. (C++ namespace)
#include <cassert>
#include <cerrno>
#include <cstring>

// C++ includes.
#include <string>
#include <vector>
using std::string;
using std::vector;

namespace LibRomData {

ROMDATA_IMPL(GameCubeBNR)
ROMDATA_IMPL_IMG(GameCubeBNR)

class GameCubeBNRPrivate : public RomDataPrivate
{
	public:
		GameCubeBNRPrivate(GameCubeBNR *q, IRpFile *file);
		virtual ~GameCubeBNRPrivate();

	private:
		typedef RomDataPrivate super;
		RP_DISABLE_COPY(GameCubeBNRPrivate)

	public:
		// Banner type.
		enum RomType {
			BANNER_UNKNOWN	= -1,	// Unknown banner type.
			BANNER_BNR1	= 0,	// BNR1 (US/JP)
			BANNER_BNR2	= 1,	// BNR2 (EU)
		};

		// Banner type.
		int bannerType;

		// Internal images.
		rp_image *img_banner;

		// Banner comments.
		// - If BNR1: 1 item.
		// - If BNR2: 6 items.
		gcn_banner_comment_t *comments;

	public:
		/**
		 * Load the banner.
		 * @return Banner, or nullptr on error.
		 */
		const rp_image *loadBanner(void);
};

/** GameCubeBNRPrivate **/

GameCubeBNRPrivate::GameCubeBNRPrivate(GameCubeBNR *q, IRpFile *file)
	: super(q, file)
	, bannerType(BANNER_UNKNOWN)
	, img_banner(nullptr)
	, comments(nullptr)
{ }

GameCubeBNRPrivate::~GameCubeBNRPrivate()
{
	delete img_banner;
	delete[] comments;
}

/**
 * Load the save file's banner.
 * @return Banner, or nullptr on error.
 */
const rp_image *GameCubeBNRPrivate::loadBanner(void)
{
	if (img_banner) {
		// Banner is already loaded.
		return img_banner;
	} else if (!this->file || !this->isValid) {
		// Can't load the banner.
		return nullptr;
	}

	// Banner is located at 0x0020.
	auto bannerbuf = aligned_uptr<uint16_t>(16, GCN_BANNER_IMAGE_SIZE/2);
	size_t size = file->seekAndRead(offsetof(gcn_banner_bnr1_t, banner), bannerbuf.get(), GCN_BANNER_IMAGE_SIZE);
	if (size != GCN_BANNER_IMAGE_SIZE) {
		// Seek and/or read error.
		return nullptr;
	}

	// Convert the banner from GCN RGB5A3 format to ARGB32.
	img_banner = ImageDecoder::fromGcn16(ImageDecoder::PXF_RGB5A3,
		GCN_BANNER_IMAGE_W, GCN_BANNER_IMAGE_H,
		bannerbuf.get(), GCN_BANNER_IMAGE_SIZE);
	return img_banner;
}

/** GameCubeBNR **/

/**
 * Read a Nintendo GameCube banner file.
 *
 * A save file must be opened by the caller. The file handle
 * will be dup()'d and must be kept open in order to load
 * data from the disc image.
 *
 * To close the file, either delete this object or call close().
 *
 * NOTE: Check isValid() to determine if this is a valid ROM.
 *
 * @param file Open disc image.
 */
GameCubeBNR::GameCubeBNR(IRpFile *file)
	: super(new GameCubeBNRPrivate(this, file))
{
	// This class handles save files.
	// NOTE: This will be handled using the same
	// settings as GameCube.
	RP_D(GameCubeBNR);
	d->className = "GameCube";
	d->fileType = FTYPE_BANNER_FILE;

	if (!d->file) {
		// Could not dup() the file handle.
		return;
	}

	// Read the magic number.
	uint32_t bnr_magic;
	d->file->rewind();
	size_t size = d->file->read(&bnr_magic, sizeof(bnr_magic));
	if (size != sizeof(bnr_magic)) {
		delete d->file;
		d->file = nullptr;
		return;
	}

	// Check if this file is supported.
	DetectInfo info;
	info.header.addr = 0;
	info.header.size = sizeof(bnr_magic);
	info.header.pData = reinterpret_cast<const uint8_t*>(&bnr_magic);
	info.ext = nullptr;	// Not needed for GameCube banner files.
	info.szFile = d->file->size();
	d->bannerType = isRomSupported_static(&info);
	d->isValid = (d->bannerType >= 0);

	if (!d->isValid) {
		delete d->file;
		d->file = nullptr;
		return;
	}

	// Read the banner comments.
	unsigned int num;
	switch (d->bannerType) {
		default:
			// Unknown banner type.
			num = 0;
			break;
		case GameCubeBNRPrivate::BANNER_BNR1:
			// US/JP: One comment.
			num = 1;
			break;
		case GameCubeBNRPrivate::BANNER_BNR2:
			// PAL: Six comments.
			num = 6;
			break;
	}

	if (num > 0) {
		// Read the comments.
		d->comments = new gcn_banner_comment_t[num];
		const size_t expSize = sizeof(gcn_banner_comment_t) * num;
		size = file->seekAndRead(offsetof(gcn_banner_bnr1_t, comment), d->comments, expSize);
		if (size != expSize) {
			// Seek and/or read error.
			delete[] d->comments;
			d->comments = nullptr;
		}
	}
}

/** ROM detection functions. **/

/**
 * Is a ROM image supported by this class?
 * @param info DetectInfo containing ROM detection information.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int GameCubeBNR::isRomSupported_static(const DetectInfo *info)
{
	assert(info != nullptr);
	assert(info->header.pData != nullptr);
	assert(info->header.addr == 0);
	if (!info || !info->header.pData ||
	    info->header.addr != 0 ||
	    info->header.size < sizeof(uint32_t))
	{
		// Either no detection information was specified,
		// or the header is too small.
		return -1;
	}

	const uint32_t bnr_magic = be32_to_cpu(
		*(reinterpret_cast<const uint32_t*>(info->header.pData)));

	switch (bnr_magic) {
		case GCN_BANNER_MAGIC_BNR1:
			if (info->szFile >= (int64_t)sizeof(gcn_banner_bnr1_t)) {
				// This is BNR1.
				return GameCubeBNRPrivate::BANNER_BNR1;
			}
			break;
		case GCN_BANNER_MAGIC_BNR2:
			if (info->szFile >= (int64_t)sizeof(gcn_banner_bnr2_t)) {
				// This is BNR2.
				return GameCubeBNRPrivate::BANNER_BNR2;
			}
			// TODO: If size is >= BNR1 but not BNR2, handle as BNR1?
			break;
		default:
			break;
	}

	// Not suported.
	return -1;
}

/**
 * Get the name of the system the loaded ROM is designed for.
 * @param type System name type. (See the SystemName enum.)
 * @return System name, or nullptr if type is invalid.
 */
const char *GameCubeBNR::systemName(unsigned int type) const
{
	RP_D(const GameCubeBNR);
	if (!d->isValid || !isSystemNameTypeValid(type))
		return nullptr;

	// Bits 0-1: Type. (long, short, abbreviation)
	static const char *const sysNames[4] = {
		// FIXME: "NGC" in Japan?
		"Nintendo GameCube", "GameCube", "GCN", nullptr,
	};

	return sysNames[type & SYSNAME_TYPE_MASK];
}

/**
 * Get a list of all supported file extensions.
 * This is to be used for file type registration;
 * subclasses don't explicitly check the extension.
 *
 * NOTE: The extensions do not include the leading dot,
 * e.g. "bin" instead of ".bin".
 *
 * NOTE 2: The array and the strings in the array should
 * *not* be freed by the caller.
 *
 * @return NULL-terminated array of all supported file extensions, or nullptr on error.
 */
const char *const *GameCubeBNR::supportedFileExtensions_static(void)
{
	// Banner is usually "opening.bnr" in the disc's root directory.
	static const char *const exts[] = {
		".bnr",

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
const char *const *GameCubeBNR::supportedMimeTypes_static(void)
{
	static const char *const mimeTypes[] = {
		// Unofficial MIME types.
		// TODO: Get these upstreamed on FreeDesktop.org.
		"application/x-gamecube-bnr",	// .bnr

		nullptr
	};
	return mimeTypes;
}

/**
 * Get a bitfield of image types this class can retrieve.
 * @return Bitfield of supported image types. (ImageTypesBF)
 */
uint32_t GameCubeBNR::supportedImageTypes_static(void)
{
	return IMGBF_INT_BANNER;
}

/**
 * Get a list of all available image sizes for the specified image type.
 *
 * The first item in the returned vector is the "default" size.
 * If the width/height is 0, then an image exists, but the size is unknown.
 *
 * @param imageType Image type.
 * @return Vector of available image sizes, or empty vector if no images are available.
 */
vector<RomData::ImageSizeDef> GameCubeBNR::supportedImageSizes_static(ImageType imageType)
{
	ASSERT_supportedImageSizes(imageType);

	if (imageType != IMG_INT_BANNER) {
		// Only banners are supported.
		return vector<ImageSizeDef>();
	}

	static const ImageSizeDef sz_INT_BANNER[] = {
		{nullptr, GCN_BANNER_IMAGE_W, GCN_BANNER_IMAGE_H, 0},
	};
	return vector<ImageSizeDef>(sz_INT_BANNER,
		sz_INT_BANNER + ARRAY_SIZE(sz_INT_BANNER));
}

/**
 * Get image processing flags.
 *
 * These specify post-processing operations for images,
 * e.g. applying transparency masks.
 *
 * @param imageType Image type.
 * @return Bitfield of ImageProcessingBF operations to perform.
 */
uint32_t GameCubeBNR::imgpf(ImageType imageType) const
{
	ASSERT_imgpf(imageType);

	if (imageType == IMG_INT_BANNER) {
		// Use nearest-neighbor scaling.
		return IMGPF_RESCALE_NEAREST;
	}

	// Nothing else is supported.
	return 0;
}

/**
 * Load field data.
 * Called by RomData::fields() if the field data hasn't been loaded yet.
 * @return Number of fields read on success; negative POSIX error code on error.
 */
int GameCubeBNR::loadFieldData(void)
{
	RP_D(GameCubeBNR);
	if (!d->fields->empty()) {
		// Field data *has* been loaded...
		return 0;
	} else if (!d->file || !d->file->isOpen()) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid || d->bannerType < 0) {
		// Unknown banner file type.
		return -EIO;
	}

	// Get the comment.
	const gcn_banner_comment_t *comment = getComment();
	if (!comment) {
		// No comment...
		return static_cast<int>(d->fields->count());
	}
	d->fields->reserve(3);	// Maximum of 3 fields.

	// TODO: If BNR1, check for Shift-JIS characters.
	// Assuming cp1252 for now.

	// TODO: Show both full and normal?
	// Currently showing full if it's there; otherwise, normal.

	// Game name.
	if (comment->gamename_full[0] != '\0') {
		d->fields->addField_string(C_("GameCubeBNR", "Game Name"),
			cp1252_to_utf8(comment->gamename_full, sizeof(comment->gamename_full)));
	} else if (comment->gamename[0] != '\0') {
		d->fields->addField_string(C_("GameCubeBNR", "Game Name"),
			cp1252_to_utf8(comment->gamename, sizeof(comment->gamename)));
	}

	// Company.
	if (comment->company_full[0] != '\0') {
		d->fields->addField_string(C_("GameCubeBNR", "Company"),
			cp1252_to_utf8(comment->company_full, sizeof(comment->company_full)));
	} else if (comment->company[0] != '\0') {
		d->fields->addField_string(C_("GameCubeBNR", "Company"),
			cp1252_to_utf8(comment->company, sizeof(comment->company)));
	}

	// Game description.
	if (comment->gamedesc[0] != '\0') {
		d->fields->addField_string(C_("GameCubeBNR", "Description"),
			cp1252_to_utf8(comment->gamedesc, sizeof(comment->gamedesc)));
	}

	// Finished reading the field data.
	return static_cast<int>(d->fields->count());
}

/**
 * Load metadata properties.
 * Called by RomData::metaData() if the field data hasn't been loaded yet.
 * @return Number of metadata properties read on success; negative POSIX error code on error.
 */
int GameCubeBNR::loadMetaData(void)
{
	RP_D(GameCubeBNR);
	if (d->metaData != nullptr) {
		// Metadata *has* been loaded...
		return 0;
	} else if (!d->file) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid || d->bannerType < 0) {
		// Unknown banner file type.
		return -EIO;
	}

	// Get the comment.
	const gcn_banner_comment_t *const comment = getComment();
	if (!comment) {
		// No comment...
		return 0;
	}

	// Create the metadata object.
	d->metaData = new RomMetaData();
	d->metaData->reserve(3);	// Maximum of 3 metadata properties.

	// TODO: If BNR1, check for Shift-JIS characters.
	// Assuming cp1252 for now.

	// TODO: Show both full and normal?
	// Currently showing full if it's there; otherwise, normal.

	// Game name.
	if (comment->gamename_full[0] != '\0') {
		d->metaData->addMetaData_string(Property::Title,
			cp1252_to_utf8(comment->gamename_full, sizeof(comment->gamename_full)));
	} else if (comment->gamename[0] != '\0') {
		d->metaData->addMetaData_string(Property::Title,
			cp1252_to_utf8(comment->gamename_full, sizeof(comment->gamename_full)));
	}

	// Company.
	if (comment->company_full[0] != '\0') {
		d->metaData->addMetaData_string(Property::Publisher,
			cp1252_to_utf8(comment->company_full, sizeof(comment->company_full)));
	} else if (comment->company[0] != '\0') {
		d->metaData->addMetaData_string(Property::Publisher,
			cp1252_to_utf8(comment->company, sizeof(comment->company)));
	}

	// Game description.
	if (comment->gamedesc[0] != '\0') {
		// TODO: Property::Comment is assumed to be user-added
		// on KDE Dolphin 18.08.1. Needs a description property.
		// Also needs verification on Windows.
		d->metaData->addMetaData_string(Property::Subject,
			cp1252_to_utf8(comment->gamedesc, sizeof(comment->gamedesc)));
	}

	// Finished reading the metadata.
	return static_cast<int>(d->metaData->count());
}

/**
 * Load an internal image.
 * Called by RomData::image().
 * @param imageType	[in] Image type to load.
 * @param pImage	[out] Pointer to const rp_image* to store the image in.
 * @return 0 on success; negative POSIX error code on error.
 */
int GameCubeBNR::loadInternalImage(ImageType imageType, const rp_image **pImage)
{
	ASSERT_loadInternalImage(imageType, pImage);

	if (imageType != IMG_INT_BANNER) {
		// Only IMG_INT_BANNER is supported by GameCubeBNR.
		*pImage = nullptr;
		return -ENOENT;
	}

	RP_D(GameCubeBNR);
	if (d->img_banner) {
		// Banner is loaded.
		*pImage = d->img_banner;
		return 0;
	}

	if (!d->file) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid || d->bannerType < 0) {
		// Banner file isn't valid.
		return -EIO;
	}

	// Load the image.
	// TODO: -ENOENT if the file doesn't actually have an icon/banner.
	*pImage = d->loadBanner();
	return (*pImage != nullptr ? 0 : -EIO);
}

/** GameCubeBNR accessors. **/

/**
 * Get the gcn_banner_comment_t.
 *
 * For BNR2, this returns the comment that most closely
 * matches the system language.
 *
 * return gcn_banner_comment_t, or nullptr on error.
 */
const gcn_banner_comment_t *GameCubeBNR::getComment(void) const
{
	RP_D(const GameCubeBNR);
	assert(d->comments != nullptr);
	if (!d->comments) {
		// No comments available...
		return nullptr;
	}

	const gcn_banner_comment_t *comment;
	switch (d->bannerType) {
		default:
			// Unknown banner type.
			comment = nullptr;
			break;

		case GameCubeBNRPrivate::BANNER_BNR1:
			// US/JP: One comment.
			comment = d->comments;
			break;

		case GameCubeBNRPrivate::BANNER_BNR2: {
			// PAL: Six comments.
			// Get the system language.
			const int lang = NintendoLanguage::getGcnPalLanguage();
			comment = &d->comments[lang];

			// If all of the language-specific fields are empty,
			// revert to English.
			if (comment->gamename[0] == 0 &&
			    comment->company[0] == 0 &&
			    comment->gamename_full[0] == 0 &&
			    comment->company_full[0] == 0 &&
			    comment->gamedesc[0] == 0)
			{
				// Revert to English.
				comment = &d->comments[GCN_PAL_LANG_ENGLISH];
			}
			break;
		}
	}

	return comment;
}

}
