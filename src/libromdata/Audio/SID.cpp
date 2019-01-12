/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * SID.hpp: SID audio reader.                                              *
 *                                                                         *
 * Copyright (c) 2018 by David Korth.                                      *
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

#include "SID.hpp"
#include "librpbase/RomData_p.hpp"

#include "sid_structs.h"

// librpbase
#include "librpbase/common.h"
#include "librpbase/byteswap.h"
#include "librpbase/TextFuncs.hpp"
#include "librpbase/file/IRpFile.hpp"
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

ROMDATA_IMPL(SID)

class SIDPrivate : public RomDataPrivate
{
	public:
		SIDPrivate(SID *q, IRpFile *file);

	private:
		typedef RomDataPrivate super;
		RP_DISABLE_COPY(SIDPrivate)

	public:
		// SID header.
		// NOTE: **NOT** byteswapped in memory.
		SID_Header sidHeader;
};

/** SIDPrivate **/

SIDPrivate::SIDPrivate(SID *q, IRpFile *file)
	: super(q, file)
{
	// Clear the SID header struct.
	memset(&sidHeader, 0, sizeof(sidHeader));
}

/** SID **/

/**
 * Read an SID audio file.
 *
 * A ROM image must be opened by the caller. The file handle
 * will be dup()'d and must be kept open in order to load
 * data from the ROM image.
 *
 * To close the file, either delete this object or call close().
 *
 * NOTE: Check isValid() to determine if this is a valid ROM.
 *
 * @param file Open ROM image.
 */
SID::SID(IRpFile *file)
	: super(new SIDPrivate(this, file))
{
	RP_D(SID);
	d->className = "SID";
	d->fileType = FTYPE_AUDIO_FILE;

	if (!d->file) {
		// Could not dup() the file handle.
		return;
	}

	// Read the SID header.
	d->file->rewind();
	size_t size = d->file->read(&d->sidHeader, sizeof(d->sidHeader));
	if (size != sizeof(d->sidHeader)) {
		delete d->file;
		d->file = nullptr;
		return;
	}

	// Check if this file is supported.
	DetectInfo info;
	info.header.addr = 0;
	info.header.size = sizeof(d->sidHeader);
	info.header.pData = reinterpret_cast<const uint8_t*>(&d->sidHeader);
	info.ext = nullptr;	// Not needed for SID.
	info.szFile = 0;	// Not needed for SID.
	d->isValid = (isRomSupported_static(&info) >= 0);

	if (!d->isValid) {
		delete d->file;
		d->file = nullptr;
		return;
	}
}

/**
 * Is a ROM image supported by this class?
 * @param info DetectInfo containing ROM detection information.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int SID::isRomSupported_static(const DetectInfo *info)
{
	assert(info != nullptr);
	assert(info->header.pData != nullptr);
	assert(info->header.addr == 0);
	if (!info || !info->header.pData ||
	    info->header.addr != 0 ||
	    info->header.size < sizeof(SID_Header))
	{
		// Either no detection information was specified,
		// or the header is too small.
		return -1;
	}

	const SID_Header *const sidHeader =
		reinterpret_cast<const SID_Header*>(info->header.pData);

	// Check the SID magic number.
	if (sidHeader->magic == cpu_to_be32(PSID_MAGIC) ||
	    sidHeader->magic == cpu_to_be32(RSID_MAGIC))
	{
		// Found the SID magic number.
		// TODO: Differentiate between PSID and RSID here?
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
const char *SID::systemName(unsigned int type) const
{
	RP_D(const SID);
	if (!d->isValid || !isSystemNameTypeValid(type))
		return nullptr;

	// SID has the same name worldwide, so we can
	// ignore the region selection.
	static_assert(SYSNAME_TYPE_MASK == 3,
		"SID::systemName() array index optimization needs to be updated.");

	static const char *const sysNames[4] = {
		"Commodore 64 SID Music",
		"SID",
		"SID",
		nullptr
	};

	return sysNames[type & SYSNAME_TYPE_MASK];
}

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
const char *const *SID::supportedFileExtensions_static(void)
{
	static const char *const exts[] = {
		".sid", ".psid",

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
const char *const *SID::supportedMimeTypes_static(void)
{
	static const char *const mimeTypes[] = {
		// Official MIME types.
		"audio/prs.sid",

		nullptr
	};
	return mimeTypes;
}

/**
 * Load field data.
 * Called by RomData::fields() if the field data hasn't been loaded yet.
 * @return Number of fields read on success; negative POSIX error code on error.
 */
int SID::loadFieldData(void)
{
	RP_D(SID);
	if (!d->fields->empty()) {
		// Field data *has* been loaded...
		return 0;
	} else if (!d->file) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid) {
		// Unknown file type.
		return -EIO;
	}

	// SID header.
	const SID_Header *const sidHeader = &d->sidHeader;
	d->fields->reserve(10);	// Maximum of 10 fields.

	// Type.
	const char *type;
	switch (be32_to_cpu(sidHeader->magic)) {
		case PSID_MAGIC:
			type = "PlaySID";
			break;
		case RSID_MAGIC:
			type = "RealSID";
			break;
		default:
			// Should not happen...
			assert(!"Invalid SID type.");
			type = "Unknown";
			break;
	}
	d->fields->addField_string(C_("SID", "Type"), type);

	// Version.
	// TODO: Check for PSIDv2NG?
	d->fields->addField_string_numeric(C_("RomData", "Version"),
		be16_to_cpu(sidHeader->version));

	// Name.
	if (sidHeader->name[0] != 0) {
		d->fields->addField_string(C_("RomData|Audio", "Name"),
			latin1_to_utf8(sidHeader->name, sizeof(sidHeader->name)));
	}

	// Author.
	if (sidHeader->author[0] != 0) {
		d->fields->addField_string(C_("RomData|Audio", "Author"),
			latin1_to_utf8(sidHeader->author, sizeof(sidHeader->author)));
	}

	// Copyright.
	if (sidHeader->copyright[0] != 0) {
		d->fields->addField_string(C_("RomData|Audio", "Copyright"),
			latin1_to_utf8(sidHeader->copyright, sizeof(sidHeader->copyright)));
	}

	// Load address.
	d->fields->addField_string_numeric(C_("SID", "Load Address"),
		be16_to_cpu(sidHeader->loadAddress),
		RomFields::FB_HEX, 4, RomFields::STRF_MONOSPACE);

	// Init address.
	d->fields->addField_string_numeric(C_("SID", "Init Address"),
		be16_to_cpu(sidHeader->initAddress),
		RomFields::FB_HEX, 4, RomFields::STRF_MONOSPACE);

	// Play address.
	d->fields->addField_string_numeric(C_("SID", "Play Address"),
		be16_to_cpu(sidHeader->playAddress),
		RomFields::FB_HEX, 4, RomFields::STRF_MONOSPACE);

	// Number of songs.
	d->fields->addField_string_numeric(C_("RomData|Audio", "# of Songs"),
		be16_to_cpu(sidHeader->songs));

	// Starting song number.
	d->fields->addField_string_numeric(C_("RomData|Audio", "Starting Song #"),
		be16_to_cpu(sidHeader->startSong));

	// TODO: Speed?
	// TODO: v2+ fields.

	// Finished reading the field data.
	return static_cast<int>(d->fields->count());
}

/**
 * Load metadata properties.
 * Called by RomData::metaData() if the field data hasn't been loaded yet.
 * @return Number of metadata properties read on success; negative POSIX error code on error.
 */
int SID::loadMetaData(void)
{
	RP_D(SID);
	if (d->metaData != nullptr) {
		// Metadata *has* been loaded...
		return 0;
	} else if (!d->file) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid) {
		// Unknown file type.
		return -EIO;
	}

	// Create the metadata object.
	d->metaData = new RomMetaData();
	d->metaData->reserve(3);	// Maximum of 3 metadata properties.

	// SID header.
	const SID_Header *const sidHeader = &d->sidHeader;

	// Title. (Name)
	if (sidHeader->name[0] != 0) {
		d->metaData->addMetaData_string(Property::Title,
			latin1_to_utf8(sidHeader->name, sizeof(sidHeader->name)));
	}

	// Author.
	if (sidHeader->author[0] != 0) {
		// TODO: Composer instead of Author?
		d->metaData->addMetaData_string(Property::Author,
			latin1_to_utf8(sidHeader->author, sizeof(sidHeader->author)));
	}

	// Copyright.
	if (sidHeader->copyright[0] != 0) {
		d->metaData->addMetaData_string(Property::Copyright,
			latin1_to_utf8(sidHeader->copyright, sizeof(sidHeader->copyright)));
	}

	// Finished reading the metadata.
	return static_cast<int>(d->fields->count());
}

}
