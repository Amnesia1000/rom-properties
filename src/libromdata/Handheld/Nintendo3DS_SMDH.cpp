/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * Nintendo3DS_SMDH.hpp: Nintendo 3DS SMDH reader.                         *
 * Handles SMDH files and SMDH sections.                                   *
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

#include "librpbase/config.librpbase.h"

#include "Nintendo3DS_SMDH.hpp"
#include "librpbase/RomData_p.hpp"

#include "n3ds_structs.h"
#include "data/NintendoLanguage.hpp"

// librpbase
#include "librpbase/common.h"
#include "librpbase/byteswap.h"
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

ROMDATA_IMPL(Nintendo3DS_SMDH)
ROMDATA_IMPL_IMG_TYPES(Nintendo3DS_SMDH)
ROMDATA_IMPL_IMG_SIZES(Nintendo3DS_SMDH)

// Workaround for RP_D() expecting the no-underscore naming convention.
#define Nintendo3DS_SMDHPrivate Nintendo3DS_SMDH_Private

class Nintendo3DS_SMDH_Private : public RomDataPrivate
{
	public:
		Nintendo3DS_SMDH_Private(Nintendo3DS_SMDH *q, IRpFile *file);
		virtual ~Nintendo3DS_SMDH_Private();

	private:
		typedef RomDataPrivate super;
		RP_DISABLE_COPY(Nintendo3DS_SMDH_Private)

	public:
		// Internal images.
		// 0 == 24x24; 1 == 48x48
		rp_image *img_icon[2];

	public:
		// SMDH headers.
		struct {
			N3DS_SMDH_Header_t header;
			N3DS_SMDH_Icon_t icon;
		} smdh;

		/**
		 * Load the ROM image's icon.
		 * @param idx Image index. (0 == 24x24; 1 == 48x48)
		 * @return Icon, or nullptr on error.
		 */
		const rp_image *loadIcon(int idx = 1);

		/**
		 * Get the language ID to use for the title fields.
		 * @return N3DS language ID.
		 */
		N3DS_Language_ID getLangID(void) const;
};

/** Nintendo3DS_SMDH_Private **/

Nintendo3DS_SMDH_Private::Nintendo3DS_SMDH_Private(Nintendo3DS_SMDH *q, IRpFile *file)
	: super(q, file)
{
	// Clear img_icon.
	img_icon[0] = nullptr;
	img_icon[1] = nullptr;

	// Clear the SMDH headers.
	memset(&smdh, 0, sizeof(smdh));
}

Nintendo3DS_SMDH_Private::~Nintendo3DS_SMDH_Private()
{
	delete img_icon[0];
	delete img_icon[1];
}

/**
 * Load the ROM image's icon.
 * @param idx Image index. (0 == 24x24; 1 == 48x48)
 * @return Icon, or nullptr on error.
 */
const rp_image *Nintendo3DS_SMDH_Private::loadIcon(int idx)
{
	assert(idx == 0 || idx == 1);
	if (idx != 0 && idx != 1) {
		// Invalid icon index.
		return nullptr;
	}

	if (img_icon[idx]) {
		// Icon has already been loaded.
		return img_icon[idx];
	} else if (!file || !isValid) {
		// Can't load the icon.
		return nullptr;
	}

	// Make sure the SMDH section is loaded.
	if (smdh.header.magic != cpu_to_be32(N3DS_SMDH_HEADER_MAGIC)) {
		// Not loaded. Cannot load an icon.
		return nullptr;
	}

	// Convert the icon to rp_image.
	// NOTE: Assuming RGB565 format.
	// 3dbrew.org says it could be any of various formats,
	// but only RGB565 has been used so far.
	// Reference: https://www.3dbrew.org/wiki/SMDH#Icon_graphics
	switch (idx) {
		case 0:
			// Small icon. (24x24)
			// NOTE: Some older homebrew, including RxTools,
			// has a broken 24x24 icon.
			img_icon[0] = ImageDecoder::fromN3DSTiledRGB565(
				N3DS_SMDH_ICON_SMALL_W, N3DS_SMDH_ICON_SMALL_H,
				smdh.icon.small, sizeof(smdh.icon.small));
			break;
		case 1:
			// Large icon. (48x48)
			img_icon[1] = ImageDecoder::fromN3DSTiledRGB565(
				N3DS_SMDH_ICON_LARGE_W, N3DS_SMDH_ICON_LARGE_H,
				smdh.icon.large, sizeof(smdh.icon.large));
			break;
		default:
			// Invalid icon index.
			assert(!"Invalid 3DS icon index.");
			return nullptr;
	}

	return img_icon[idx];
}

/**
 * Get the language ID to use for the title fields.
 * @return N3DS language ID.
 */
N3DS_Language_ID Nintendo3DS_SMDH_Private::getLangID(void) const
{
	// Get the system language.
	// TODO: Verify against the game's region code?
	N3DS_Language_ID lang = static_cast<N3DS_Language_ID>(NintendoLanguage::getN3DSLanguage());

	// Check that the field is valid.
	if (smdh.header.titles[lang].desc_short[0] == cpu_to_le16(0)) {
		// Not valid. Check English.
		if (smdh.header.titles[N3DS_LANG_ENGLISH].desc_short[0] != cpu_to_le16(0)) {
			// English is valid.
			lang = N3DS_LANG_ENGLISH;
		} else {
			// Not valid. Check Japanese.
			if (smdh.header.titles[N3DS_LANG_JAPANESE].desc_short[0] != cpu_to_le16(0)) {
				// Japanese is valid.
				lang = N3DS_LANG_JAPANESE;
			} else {
				// Not valid...
				// Default to English anyway.
				lang = N3DS_LANG_ENGLISH;
			}
		}
	}

	return lang;
}

/** Nintendo3DS_SMDH **/

/**
 * Read a Nintendo 3DS SMDH file and/or section.
 *
 * A ROM image must be opened by the caller. The file handle
 * will be dup()'d and must be kept open in order to load
 * data from the disc image.
 *
 * To close the file, either delete this object or call close().
 *
 * NOTE: Check isValid() to determine if this is a valid ROM.
 *
 * @param file Open SMDH file and/or section..
 */
Nintendo3DS_SMDH::Nintendo3DS_SMDH(IRpFile *file)
	: super(new Nintendo3DS_SMDH_Private(this, file))
{
	// This class handles SMDH files and/or sections only.
	RP_D(Nintendo3DS_SMDH);
	d->className = "Nintendo3DS";	// Using the same image settings as Nintendo3DS.
	d->fileType = FTYPE_ICON_FILE;

	if (!d->file) {
		// Could not dup() the file handle.
		return;
	}

	// Read the SMDH section.
	d->file->rewind();
	size_t size = d->file->read(&d->smdh, sizeof(d->smdh));
	if (size != sizeof(d->smdh)) {
		d->smdh.header.magic = 0;
		delete d->file;
		d->file = nullptr;
		return;
	}

	// Check if this ROM image is supported.
	DetectInfo info;
	info.header.addr = 0;
	info.header.size = sizeof(d->smdh);
	info.header.pData = reinterpret_cast<const uint8_t*>(&d->smdh);
	info.ext = nullptr;	// Not needed for Nintendo3DS_SMDH.
	info.szFile = 0;	// Not needed for Nintendo3DS_SMDH.
	d->isValid = (isRomSupported_static(&info) >= 0);

	if (!d->isValid) {
		d->smdh.header.magic = 0;
		delete d->file;
		d->file = nullptr;
		return;
	}
}

/** ROM detection functions. **/

/**
 * Is a ROM image supported by this class?
 * @param info DetectInfo containing ROM detection information.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int Nintendo3DS_SMDH::isRomSupported_static(const DetectInfo *info)
{
	assert(info != nullptr);
	assert(info->header.pData != nullptr);
	assert(info->header.addr == 0);
	if (!info || !info->header.pData ||
	    info->header.addr != 0 ||
	    info->header.size < 512)
	{
		// Either no detection information was specified,
		// or the header is too small.
		return -1;
	}

	// Check for SMDH.
	const N3DS_SMDH_Header_t *const smdhHeader =
		reinterpret_cast<const N3DS_SMDH_Header_t*>(info->header.pData);
	if (smdhHeader->magic == cpu_to_be32(N3DS_SMDH_HEADER_MAGIC)) {
		// We have an SMDH file.
		return 0;
	}

	// Not supported.
	return -1;
}

/**
 * Get the name of the system the loaded ROM is designed for.
 * @param type System name type. (See the SystemName enum.)
 * @return System name, or nullptr if type is invalid.
 */
const char *Nintendo3DS_SMDH::systemName(unsigned int type) const
{
	RP_D(const Nintendo3DS_SMDH);
	if (!d->isValid || !isSystemNameTypeValid(type))
		return nullptr;

	// Bits 0-1: Type. (long, short, abbreviation)
	// TODO: SMDH-specific, or just use Nintendo 3DS?
	static const char *const sysNames[4] = {
		"Nintendo 3DS", "Nintendo 3DS", "3DS", nullptr
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
const char *const *Nintendo3DS_SMDH::supportedFileExtensions_static(void)
{
	static const char *const exts[] = {
		".smdh",	// SMDH (icon) file.

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
const char *const *Nintendo3DS_SMDH::supportedMimeTypes_static(void)
{
	static const char *const mimeTypes[] = {
		// Unofficial MIME types.
		// TODO: Get these upstreamed on FreeDesktop.org.
		"application/x-nintendo-3ds-smdh",

		nullptr
	};
	return mimeTypes;
}

/**
 * Get a bitfield of image types this class can retrieve.
 * @return Bitfield of supported image types. (ImageTypesBF)
 */
uint32_t Nintendo3DS_SMDH::supportedImageTypes_static(void)
{
	return IMGBF_INT_ICON;
}

/**
 * Get a list of all available image sizes for the specified image type.
 * @param imageType Image type.
 * @return Vector of available image sizes, or empty vector if no images are available.
 */
vector<RomData::ImageSizeDef> Nintendo3DS_SMDH::supportedImageSizes_static(ImageType imageType)
{
	ASSERT_supportedImageSizes(imageType);

	if (imageType != IMG_INT_ICON) {
		// Only icons are supported.
		return vector<ImageSizeDef>();
	}

	static const ImageSizeDef sz_INT_ICON[] = {
		{nullptr, 24, 24, 0},
		{nullptr, 48, 48, 1},
	};
	return vector<ImageSizeDef>(sz_INT_ICON,
		sz_INT_ICON + ARRAY_SIZE(sz_INT_ICON));
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
uint32_t Nintendo3DS_SMDH::imgpf(ImageType imageType) const
{
	ASSERT_imgpf(imageType);

	uint32_t ret = 0;
	switch (imageType) {
		case IMG_INT_ICON:
			// Use nearest-neighbor scaling.
			ret = IMGPF_RESCALE_NEAREST;
			break;
		default:
			break;
	}
	return ret;
}

/**
 * Load field data.
 * Called by RomData::fields() if the field data hasn't been loaded yet.
 * @return Number of fields read on success; negative POSIX error code on error.
 */
int Nintendo3DS_SMDH::loadFieldData(void)
{
	RP_D(Nintendo3DS_SMDH);
	if (!d->fields->empty()) {
		// Field data *has* been loaded...
		return 0;
	} else if (!d->file || !d->file->isOpen()) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid) {
		// SMDH file isn't valid.
		return -EIO;
	}

	// NOTE: Using "Nintendo3DS" as the localization context.

	// Parse the SMDH header.
	const N3DS_SMDH_Header_t *const smdhHeader = &d->smdh.header;
	if (smdhHeader->magic != cpu_to_be32(N3DS_SMDH_HEADER_MAGIC)) {
		// Invalid magic number.
		return 0;
	}

	// Maximum of 5 fields.
	d->fields->reserve(5);
	d->fields->setTabName(0, "SMDH");

	// Title fields.
	N3DS_Language_ID lang = d->getLangID();
	if (smdhHeader->titles[lang].desc_short[0] != '\0') {
		d->fields->addField_string(C_("Nintendo3DS", "Title"), utf16le_to_utf8(
			smdhHeader->titles[lang].desc_short, ARRAY_SIZE(smdhHeader->titles[lang].desc_short)));
	}
	if (smdhHeader->titles[lang].desc_long[0] != '\0') {
		d->fields->addField_string(C_("Nintendo3DS", "Full Title"), utf16le_to_utf8(
			smdhHeader->titles[lang].desc_long, ARRAY_SIZE(smdhHeader->titles[lang].desc_long)));
	}
	if (smdhHeader->titles[lang].publisher[0] != '\0') {
		d->fields->addField_string(C_("RomData", "Publisher"), utf16le_to_utf8(
			smdhHeader->titles[lang].publisher, ARRAY_SIZE(smdhHeader->titles[lang].publisher)));
	}

	// Region code.
	// Maps directly to the SMDH field.
	static const char *const n3ds_region_bitfield_names[] = {
		NOP_C_("Region", "Japan"),
		NOP_C_("Region", "USA"),
		NOP_C_("Region", "Europe"),
		NOP_C_("Region", "Australia"),
		NOP_C_("Region", "China"),
		NOP_C_("Region", "South Korea"),
		NOP_C_("Region", "Taiwan"),
	};
	vector<string> *const v_n3ds_region_bitfield_names = RomFields::strArrayToVector_i18n(
		"Region", n3ds_region_bitfield_names, ARRAY_SIZE(n3ds_region_bitfield_names));
	d->fields->addField_bitfield(C_("RomData", "Region Code"),
		v_n3ds_region_bitfield_names, 3, le32_to_cpu(smdhHeader->settings.region_code));

	// Age rating(s).
	// Note that not all 16 fields are present on 3DS,
	// though the fields do match exactly, so no
	// mapping is necessary.
	RomFields::age_ratings_t age_ratings;
	// Valid ratings: 0-1, 3-4, 6-10
	static const uint16_t valid_ratings = 0x7DB;

	for (int i = static_cast<int>(age_ratings.size())-1; i >= 0; i--) {
		if (!(valid_ratings & (1 << i))) {
			// Rating is not applicable for NintendoDS.
			age_ratings[i] = 0;
			continue;
		}

		// 3DS ratings field:
		// - 0x1F: Age rating.
		// - 0x20: No age restriction.
		// - 0x40: Rating pending.
		// - 0x80: Rating is valid if set.
		const uint8_t n3ds_rating = smdhHeader->settings.ratings[i];
		if (!(n3ds_rating & 0x80)) {
			// Rating is unused.
			age_ratings[i] = 0;
		} else if (n3ds_rating & 0x40) {
			// Rating pending.
			age_ratings[i] = RomFields::AGEBF_ACTIVE | RomFields::AGEBF_PENDING;
		} else if (n3ds_rating & 0x20) {
			// No age restriction.
			age_ratings[i] = RomFields::AGEBF_ACTIVE | RomFields::AGEBF_NO_RESTRICTION;
		} else {
			// Set active | age value.
			age_ratings[i] = RomFields::AGEBF_ACTIVE | (n3ds_rating & 0x1F);
		}
	}
	d->fields->addField_ageRatings(C_("RomData", "Age Ratings"), age_ratings);

	// Finished reading the field data.
	return static_cast<int>(d->fields->count());
}

/**
 * Load metadata properties.
 * Called by RomData::metaData() if the field data hasn't been loaded yet.
 * @return Number of metadata properties read on success; negative POSIX error code on error.
 */
int Nintendo3DS_SMDH::loadMetaData(void)
{
	RP_D(Nintendo3DS_SMDH);
	if (d->metaData != nullptr) {
		// Metadata *has* been loaded...
		return 0;
	} else if (!d->file) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid) {
		// SMDH file isn't valid.
		return -EIO;
	}

	// Parse the SMDH header.
	const N3DS_SMDH_Header_t *const smdhHeader = &d->smdh.header;
	if (smdhHeader->magic != cpu_to_be32(N3DS_SMDH_HEADER_MAGIC)) {
		// Invalid magic number.
		return 0;
	}

	// Create the metadata object.
	d->metaData = new RomMetaData();
	d->metaData->reserve(2);	// Maximum of 2 metadata properties.

	// Title.
	// NOTE: Preferring Full Title. If not found, using Title.
	N3DS_Language_ID lang = d->getLangID();
	if (smdhHeader->titles[lang].desc_long[0] != '\0') {
		// Using the Full Title.
		d->metaData->addMetaData_string(Property::Title,
			utf16le_to_utf8(
				smdhHeader->titles[lang].desc_long, ARRAY_SIZE(smdhHeader->titles[lang].desc_long)));
	} else if (smdhHeader->titles[lang].desc_short[0] != '\0') {
		// Using the regular Title.
		d->metaData->addMetaData_string(Property::Title,
			utf16le_to_utf8(
				smdhHeader->titles[lang].desc_short, ARRAY_SIZE(smdhHeader->titles[lang].desc_short)));
	}

	// Publisher.
	if (smdhHeader->titles[lang].publisher[0] != '\0') {
		d->metaData->addMetaData_string(Property::Publisher,
			utf16le_to_utf8(
				smdhHeader->titles[lang].publisher, ARRAY_SIZE(smdhHeader->titles[lang].publisher)));
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
int Nintendo3DS_SMDH::loadInternalImage(ImageType imageType, const rp_image **pImage)
{
	ASSERT_loadInternalImage(imageType, pImage);

	// NOTE: Assuming icon index 1. (48x48)
	const int idx = 1;

	RP_D(Nintendo3DS_SMDH);
	if (imageType != IMG_INT_ICON) {
		// Only IMG_INT_ICON is supported by 3DS.
		*pImage = nullptr;
		return -ENOENT;
	} else if (d->img_icon[idx]) {
		// Image has already been loaded.
		*pImage = d->img_icon[idx];
		return 0;
	} else if (!d->file) {
		// File isn't open.
		*pImage = nullptr;
		return -EBADF;
	} else if (!d->isValid) {
		// SMDH file isn't valid.
		*pImage = nullptr;
		return -EIO;
	}

	// Load the icon.
	*pImage = d->loadIcon(idx);
	return (*pImage != nullptr ? 0 : -EIO);
}

/** Special SMDH accessor functions. **/

/**
 * Get the SMDH region code.
 * @return SMDH region code, or 0 on error.
 */
uint32_t Nintendo3DS_SMDH::getRegionCode(void) const
{
	RP_D(const Nintendo3DS_SMDH);
	if (d->smdh.header.magic != cpu_to_be32(N3DS_SMDH_HEADER_MAGIC)) {
		// Invalid magic number.
		return 0;
	}
	return d->smdh.header.settings.region_code;
}

}
