/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * RomDataFactory.cpp: RomData factory class.                              *
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

#include "RomDataFactory.hpp"

// librpbase
#include "librpbase/common.h"
#include "librpbase/byteswap.h"
#include "librpbase/RomData.hpp"
#include "librpbase/file/IRpFile.hpp"
#include "librpbase/file/FileSystem.hpp"
#include "librpbase/file/RelatedFile.hpp"
#include "librpbase/threads/pthread_once.h"
using namespace LibRpBase;

// C includes. (C++ namespace)
#include <cassert>
#include <cstring>

// C++ includes.
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
using std::string;
using std::unordered_map;
using std::unordered_set;
using std::vector;

// RomData subclasses: Consoles
#include "Console/Dreamcast.hpp"
#include "Console/DreamcastSave.hpp"
#include "Console/GameCube.hpp"
#include "Console/GameCubeBNR.hpp"
#include "Console/GameCubeSave.hpp"
#include "Console/MegaDrive.hpp"
#include "Console/N64.hpp"
#include "Console/NES.hpp"
#include "Console/PlayStationSave.hpp"
#include "Console/Sega8Bit.hpp"
#include "Console/SegaSaturn.hpp"
#include "Console/SNES.hpp"
#include "Console/WiiSave.hpp"
#include "Console/WiiU.hpp"
#include "Console/WiiWAD.hpp"
#include "Console/WiiWIBN.hpp"

// RomData subclasses: Handhelds
#include "Handheld/DMG.hpp"
#include "Handheld/GameBoyAdvance.hpp"
#include "Handheld/GameCom.hpp"
#include "Handheld/Lynx.hpp"
#include "Handheld/Nintendo3DS.hpp"
#include "Handheld/Nintendo3DSFirm.hpp"
#include "Handheld/Nintendo3DS_SMDH.hpp"
#include "Handheld/NintendoDS.hpp"
#include "Handheld/VirtualBoy.hpp"

// RomData subclasses: Textures
#include "Texture/DirectDrawSurface.hpp"
#include "Texture/KhronosKTX.hpp"
#include "Texture/SegaPVR.hpp"
#include "Texture/ValveVTF.hpp"
#include "Texture/ValveVTF3.hpp"

// RomData subclasses: Audio
#include "Audio/ADX.hpp"
#include "Audio/GBS.hpp"
#include "Audio/NSF.hpp"
#include "Audio/PSF.hpp"
#include "Audio/SAP.hpp"
#include "Audio/SNDH.hpp"
#include "Audio/SID.hpp"
#include "Audio/SPC.hpp"
#include "Audio/VGM.hpp"

// RomData subclasses: Other
#include "Other/Amiibo.hpp"
#include "Other/ELF.hpp"
#include "Other/EXE.hpp"
#include "Other/NintendoBadge.hpp"

// Special case for Dreamcast save files.
#include "Console/dc_structs.h"

namespace LibRomData {

class RomDataFactoryPrivate
{
	private:
		RomDataFactoryPrivate();
		~RomDataFactoryPrivate();

	private:
		RP_DISABLE_COPY(RomDataFactoryPrivate)

	public:
		typedef int (*pfnIsRomSupported_t)(const RomData::DetectInfo *info);
		typedef const char *const * (*pfnSupportedFileExtensions_t)(void);
		typedef const char *const * (*pfnSupportedMimeTypes_t)(void);
		typedef RomData* (*pfnNewRomData_t)(IRpFile *file);

		struct RomDataFns {
			pfnIsRomSupported_t isRomSupported;
			pfnNewRomData_t newRomData;
			pfnSupportedFileExtensions_t supportedFileExtensions;
			pfnSupportedMimeTypes_t supportedMimeTypes;
			unsigned int attrs;

			// Extra fields for files whose headers
			// appear at specific addresses.
			uint32_t address;
			uint32_t size;	// Contains magic number for fast 32-bit magic checking.
		};

		/**
		 * Templated function to construct a new RomData subclass.
		 * @param klass Class name.
		 */
		template<typename klass>
		static LibRpBase::RomData *RomData_ctor(LibRpBase::IRpFile *file)
		{
			return new klass(file);
		}

#define GetRomDataFns(sys, attrs) \
	{sys::isRomSupported_static, \
	 RomDataFactoryPrivate::RomData_ctor<sys>, \
	 sys::supportedFileExtensions_static, \
	 sys::supportedMimeTypes_static, \
	 attrs, 0, 0}

#define GetRomDataFns_addr(sys, attrs, address, size) \
	{sys::isRomSupported_static, \
	 RomDataFactoryPrivate::RomData_ctor<sys>, \
	 sys::supportedFileExtensions_static, \
	 sys::supportedMimeTypes_static, \
	 attrs, address, size}

		// RomData subclasses that use a header at 0 and
		// definitely have a 32-bit magic number in the header.
		// - address: Address of magic number within the header.
		// - size: 32-bit magic number.
		static const RomDataFns romDataFns_magic[];

		// RomData subclasses that use a header.
		// Headers with addresses other than 0 should be
		// placed at the end of this array.
		static const RomDataFns romDataFns_header[];

		// RomData subclasses that use a footer.
		static const RomDataFns romDataFns_footer[];

		/**
		 * Attempt to open the other file in a Dreamcast .VMI+.VMS pair.
		 * @param file One opened file in the .VMI+.VMS pair.
		 * @return DreamcastSave if valid; nullptr if not.
		 */
		static RomData *openDreamcastVMSandVMI(IRpFile *file);

		// Vectors for file extensions and MIME types.
		// We want to collect them once per session instead of
		// repeatedly collecting them, since the caller might
		// not cache them.
		// pthread_once() control variable.
		static vector<RomDataFactory::ExtInfo> vec_exts;
		static vector<const char*> vec_mimeTypes;
		static pthread_once_t once_exts;
		static pthread_once_t once_mimeTypes;

		/**
		 * Initialize the vector of supported file extensions.
		 * Used for Win32 COM registration.
		 *
		 * Internal function; must be called using pthread_once().
		 *
		 * NOTE: The return value is a struct that includes a flag
		 * indicating if the file type handler supports thumbnails.
		 */
		static void init_supportedFileExtensions(void);

		/**
		 * Initialize the vector of supported MIME types.
		 * Used for KFileMetaData.
		 *
		 * Internal function; must be called using pthread_once().
		 */
		static void init_supportedMimeTypes(void);
};

/** RomDataFactoryPrivate **/

vector<RomDataFactory::ExtInfo> RomDataFactoryPrivate::vec_exts;
vector<const char*> RomDataFactoryPrivate::vec_mimeTypes;
pthread_once_t RomDataFactoryPrivate::once_exts = PTHREAD_ONCE_INIT;
pthread_once_t RomDataFactoryPrivate::once_mimeTypes = PTHREAD_ONCE_INIT;

#define ATTR_NONE RomDataFactory::RDA_NONE
#define ATTR_HAS_THUMBNAIL RomDataFactory::RDA_HAS_THUMBNAIL
#define ATTR_HAS_DPOVERLAY RomDataFactory::RDA_HAS_DPOVERLAY

// TODO: Add support for multiple magic numbers per class.
const RomDataFactoryPrivate::RomDataFns RomDataFactoryPrivate::romDataFns_magic[] = {
	// Consoles
	GetRomDataFns_addr(WiiWIBN, RomDataFactory::RDA_HAS_THUMBNAIL, 0, 'WIBN'),

	// Handhelds
	GetRomDataFns_addr(DMG, ATTR_NONE, 0x104, 0xCEED6666),
	GetRomDataFns_addr(GameBoyAdvance, ATTR_NONE, 0x04, 0x24FFAE51),
	GetRomDataFns_addr(Lynx, ATTR_NONE, 0, 'LYNX'),
	GetRomDataFns_addr(Nintendo3DSFirm, ATTR_NONE, 0, 'FIRM'),
	GetRomDataFns_addr(Nintendo3DS_SMDH, ATTR_HAS_THUMBNAIL, 0, 'SMDH'),

	// Textures
	GetRomDataFns_addr(DirectDrawSurface, ATTR_HAS_THUMBNAIL, 0, 'DDS '),
	GetRomDataFns_addr(KhronosKTX, ATTR_HAS_THUMBNAIL, 0, (uint32_t)'\xABKTX'),
	GetRomDataFns_addr(ValveVTF, ATTR_HAS_THUMBNAIL, 0, 'VTF\0'),
	GetRomDataFns_addr(ValveVTF3, ATTR_HAS_THUMBNAIL, 0, 'VTF3'),

	// Audio
	GetRomDataFns_addr(GBS, ATTR_NONE, 0, 'GBS\x01'),
	GetRomDataFns_addr(NSF, ATTR_NONE, 0, 'NESM'),
	GetRomDataFns_addr(SPC, ATTR_NONE, 0, 'SNES'),
	GetRomDataFns_addr(VGM, ATTR_NONE, 0, 'Vgm '),

	// Other
	GetRomDataFns_addr(ELF, ATTR_NONE, 0, '\177ELF'),

	{nullptr, nullptr, nullptr, nullptr, ATTR_NONE, 0, 0}
};

const RomDataFactoryPrivate::RomDataFns RomDataFactoryPrivate::romDataFns_header[] = {
	// Consoles
	GetRomDataFns(Dreamcast, ATTR_HAS_THUMBNAIL),
	GetRomDataFns(DreamcastSave, ATTR_HAS_THUMBNAIL),
	GetRomDataFns(GameCube, ATTR_HAS_THUMBNAIL),
	GetRomDataFns(GameCubeBNR, ATTR_HAS_THUMBNAIL),
	GetRomDataFns(GameCubeSave, ATTR_HAS_THUMBNAIL),
	GetRomDataFns(MegaDrive, ATTR_NONE),
	GetRomDataFns(N64, ATTR_NONE),
	GetRomDataFns(NES, ATTR_NONE),
	GetRomDataFns(SNES, ATTR_NONE),
	GetRomDataFns(SegaSaturn, ATTR_NONE),
	GetRomDataFns(WiiSave, ATTR_HAS_THUMBNAIL),
	GetRomDataFns(WiiU, ATTR_HAS_THUMBNAIL),
	GetRomDataFns(WiiWAD, ATTR_HAS_THUMBNAIL),

	// Handhelds
	GetRomDataFns(Nintendo3DS, ATTR_HAS_THUMBNAIL | ATTR_HAS_DPOVERLAY),
	GetRomDataFns(NintendoDS, ATTR_HAS_THUMBNAIL | ATTR_HAS_DPOVERLAY),

	// Textures
	GetRomDataFns(SegaPVR, ATTR_HAS_THUMBNAIL),

	// Audio
	GetRomDataFns(ADX, ATTR_NONE),
	GetRomDataFns(PSF, ATTR_NONE),
	GetRomDataFns(SAP, ATTR_NONE),	// "SAP\r\n", "SAP\n"; maybe move to _magic[]?
	GetRomDataFns(SNDH, ATTR_NONE),	// "SNDH", or "ICE!" or "Ice!" if packed.
	GetRomDataFns(SID, ATTR_NONE),	// PSID/RSID; maybe move to _magic[]?

	// Other
	GetRomDataFns(Amiibo, ATTR_HAS_THUMBNAIL),
	GetRomDataFns(NintendoBadge, ATTR_HAS_THUMBNAIL),

	// The following formats have 16-bit magic numbers,
	// so they should go at the end of the address=0 section.
	GetRomDataFns(EXE, ATTR_NONE),	// TODO: Thumbnailing on non-Windows platforms.
	GetRomDataFns(PlayStationSave, ATTR_HAS_THUMBNAIL),

	// NOTE: game.com may be at either 0 or 0x40000.
	// The 0x40000 address is checked below.
	GetRomDataFns(GameCom, ATTR_HAS_THUMBNAIL),

	// Headers with non-zero addresses.
	GetRomDataFns_addr(Sega8Bit, ATTR_NONE, 0x7FE0, 0x20),
	// NOTE: game.com may be at either 0 or 0x40000.
	// The 0 address is checked above.
	GetRomDataFns_addr(GameCom, ATTR_HAS_THUMBNAIL, 0x40000, 0x20),

	{nullptr, nullptr, nullptr, nullptr, ATTR_NONE, 0, 0}
};

const RomDataFactoryPrivate::RomDataFns RomDataFactoryPrivate::romDataFns_footer[] = {
	GetRomDataFns(VirtualBoy, ATTR_NONE),
	{nullptr, nullptr, nullptr, nullptr, ATTR_NONE, 0, 0}
};

/**
 * Attempt to open the other file in a Dreamcast .VMI+.VMS pair.
 * @param file One opened file in the .VMI+.VMS pair.
 * @return DreamcastSave if valid; nullptr if not.
 */
RomData *RomDataFactoryPrivate::openDreamcastVMSandVMI(IRpFile *file)
{
	// We're assuming the file extension was already checked.
	// VMS files are always a multiple of 512 bytes,
	// or 160 bytes for some monochrome ICONDATA_VMS.
	// VMI files are always 108 bytes;
	int64_t filesize = file->size();
	bool has_dc_vms = (filesize % DC_VMS_BLOCK_SIZE == 0) ||
			  (filesize == DC_VMS_ICONDATA_MONO_MINSIZE);
	bool has_dc_vmi = (filesize == DC_VMI_Header_SIZE);
	if (!(has_dc_vms ^ has_dc_vmi)) {
		// Can't be none or both...
		return nullptr;
	}

	// Determine which file we should look for.
	IRpFile *vms_file;
	IRpFile *vmi_file;
	IRpFile **other_file;	// Points to vms_file or vmi_file.

	const char *rel_ext;
	if (has_dc_vms) {
		// We have the VMS file.
		// Find the VMI file.
		vms_file = file;
		vmi_file = nullptr;
		other_file = &vmi_file;
		rel_ext = ".VMI";
	} else /*if (has_dc_vmi)*/ {
		// We have the VMI file.
		// Find the VMS file.
		vms_file = nullptr;
		vmi_file = file;
		other_file = &vms_file;
		rel_ext = ".VMS";
	}

	// Attempt to open the other file in the pair.
	// TODO: Verify length.
	// TODO: For .vmi, check the VMS resource name?
	const string filename = file->filename();
	*other_file = FileSystem::openRelatedFile(filename.c_str(), nullptr, rel_ext);
	if (!*other_file) {
		// Can't open the other file.
		return nullptr;
	}

	// Attempt to create a DreamcastSave using both the
	// VMS and VMI files.
	DreamcastSave *dcSave = new DreamcastSave(vms_file, vmi_file);
	delete *other_file;	// Not needed anymore.
	if (!dcSave->isValid()) {
		// Not valid.
		dcSave->unref();
		return nullptr;
	}

	// DreamcastSave opened.
	return dcSave;
}

/** RomDataFactory **/

/**
 * Create a RomData subclass for the specified ROM file.
 *
 * NOTE: RomData::isValid() is checked before returning a
 * created RomData instance, so returned objects can be
 * assumed to be valid as long as they aren't nullptr.
 *
 * If imgbf is non-zero, at least one of the specified image
 * types must be supported by the RomData subclass in order to
 * be returned.
 *
 * @param file ROM file.
 * @param attrs RomDataAttr bitfield. If set, RomData subclass must have the specified attributes.
 * @return RomData subclass, or nullptr if the ROM isn't supported.
 */
RomData *RomDataFactory::create(IRpFile *file, unsigned int attrs)
{
	RomData::DetectInfo info;

	// Get the file size.
	info.szFile = file->size();

	// Read 4,096+256 bytes from the ROM header.
	// This should be enough to detect most systems.
	union {
		uint8_t u8[4096+256];
		uint32_t u32[(4096+256)/4];
	} header;
	file->rewind();
	info.header.addr = 0;
	info.header.pData = header.u8;
	info.header.size = static_cast<uint32_t>(file->read(header.u8, sizeof(header.u8)));
	if (info.header.size == 0) {
		// Read error.
		return nullptr;
	}

	// Get the file extension.
	info.ext = nullptr;
	const string filename = file->filename();
	if (!filename.empty()) {
		info.ext = FileSystem::file_ext(filename);
	}

	// Special handling for Dreamcast .VMI+.VMS pairs.
	if (info.ext != nullptr &&
	    (!strcasecmp(info.ext, ".vms") ||
	     !strcasecmp(info.ext, ".vmi")))
	{
		// Dreamcast .VMI+.VMS pair.
		// Attempt to open the other file in the pair.
		RomData *romData = RomDataFactoryPrivate::openDreamcastVMSandVMI(file);
		if (romData) {
			if (romData->isValid()) {
				// .VMI+.VMS pair opened.
				return romData;
			}
			// Not a .VMI+.VMS pair.
			romData->unref();
		}

		// Not a .VMI+.VMS pair.
	}

	// Check RomData subclasses that take a header at 0x0000
	// and definitely have a 32-bit magic number in the header.
	const RomDataFactoryPrivate::RomDataFns *fns =
		&RomDataFactoryPrivate::romDataFns_magic[0];
	for (; fns->supportedFileExtensions != nullptr; fns++) {
		if ((fns->attrs & attrs) != attrs) {
			// This RomData subclass doesn't have the
			// required attributes.
			continue;
		}

		// Check the magic number.
		// TODO: Verify alignment restrictions.
		assert(fns->address % 4 == 0);
		assert(fns->address + sizeof(uint32_t) <= sizeof(header.u32));
		// FIXME: Fix strict aliasing warnings on Ubuntu 14.04.
		uint32_t magic = header.u32[fns->address/4];
		if (be32_to_cpu(magic) == fns->size) {
			// Found a matching magic number.
			if (fns->isRomSupported(&info) >= 0) {
				RomData *const romData = fns->newRomData(file);
				if (romData->isValid()) {
					// RomData subclass obtained.
					return romData;
				}

				// Not actually supported.
				romData->unref();
			}
		}
	}

	// Check other RomData subclasses that take a header,
	// but don't have a simple 32-bit magic number check.
	fns = &RomDataFactoryPrivate::romDataFns_header[0];
	for (; fns->supportedFileExtensions != nullptr; fns++) {
		if ((fns->attrs & attrs) != attrs) {
			// This RomData subclass doesn't have the
			// required attributes.
			continue;
		}

		if (fns->address != info.header.addr ||
		    fns->size > info.header.size)
		{
			// Header address has changed.

			// Check the file extension to reduce overhead
			// for file types that don't use this.
			// TODO: Don't hard-code this.
			// Use a pointer to supportedFileExtensions_static() instead?
			if (info.ext == nullptr) {
				// No file extension...
				break;
			} else if (strcasecmp(info.ext, ".bin") != 0 &&	/* generic .bin */
				   strcasecmp(info.ext, ".sms") != 0 &&	/* Sega Master System */
				   strcasecmp(info.ext, ".gg") != 0 &&	/* Game Gear */
				   strcasecmp(info.ext, ".tgc") != 0)	/* game.com */
			{
				// Not SMS, Game Gear, or game.com.
				break;
			}

			// Read the new header data.

			// NOTE: fns->size == 0 is only correct
			// for headers located at 0, since we
			// read the whole 4096+256 bytes for these.
			assert(fns->size != 0);
			assert(fns->size <= sizeof(header));
			if (fns->size == 0 || fns->size > sizeof(header))
				continue;

			// Make sure the file is big enough to
			// have this header.
			if ((static_cast<int64_t>(fns->address) + fns->size) > info.szFile)
				continue;

			// Read the header data.
			info.header.addr = fns->address;
			int ret = file->seek(info.header.addr);
			if (ret != 0)
				continue;
			info.header.size = static_cast<uint32_t>(file->read(header.u8, fns->size));
			if (info.header.size != fns->size)
				continue;
		}

		if (fns->isRomSupported(&info) >= 0) {
			RomData *const romData = fns->newRomData(file);
			if (romData->isValid()) {
				// RomData subclass obtained.
				return romData;
			}

			// Not actually supported.
			romData->unref();
		}
	}

	// Check RomData subclasses that take a footer.
	if (info.szFile > (1LL << 30)) {
		// No subclasses that expect footers support
		// files larger than 1 GB.
		return nullptr;
	}

	bool readFooter = false;
	fns = &RomDataFactoryPrivate::romDataFns_footer[0];
	for (; fns->supportedFileExtensions != nullptr; fns++) {
		if ((fns->attrs & attrs) != attrs) {
			// This RomData subclass doesn't have the
			// required attributes.
			continue;
		}

		// Do we have a matching extension?
		// FIXME: Instead of hard-coded, check supportedFileExtensions.
		// Currently only supports VirtualBoy.
		if (!info.ext || strcasecmp(info.ext, ".vb") != 0) {
			// Extension doesn't match.
			continue;
		}

		// Make sure we've read the footer.
		if (!readFooter) {
			static const int footer_size = 1024;
			if (info.szFile > footer_size) {
				info.header.addr = static_cast<uint32_t>(info.szFile - footer_size);
				info.header.size = static_cast<uint32_t>(file->seekAndRead(info.header.addr, header.u8, footer_size));
				if (info.header.size == 0) {
					// Seek and/or read error.
					return nullptr;
				}
			}
			readFooter = true;
		}

		if (fns->isRomSupported(&info) >= 0) {
			RomData *const romData = fns->newRomData(file);
			if (romData->isValid()) {
				// RomData subclass obtained.
				return romData;
			}

			// Not actually supported.
			romData->unref();
		}
	}

	// Not supported.
	return nullptr;
}

/**
 * Initialize the vector of supported file extensions.
 * Used for Win32 COM registration.
 *
 * Internal function; must be called using pthread_once().
 *
 * NOTE: The return value is a struct that includes a flag
 * indicating if the file type handler supports thumbnails.
 */
void RomDataFactoryPrivate::init_supportedFileExtensions(void)
{
	// In order to handle multiple RomData subclasses
	// that support the same extensions, we're using
	// an unordered_map<string, bool>. If any of the
	// handlers for a given extension support thumbnails,
	// then the thumbnail handlers will be registered.
	//
	// The actual data is stored in the vector<ExtInfo>.
	unordered_map<string, unsigned int> map_exts;

	static const size_t reserve_size =
		(ARRAY_SIZE(romDataFns_magic) +
		 ARRAY_SIZE(romDataFns_header) +
		 ARRAY_SIZE(romDataFns_footer)) * 2;
	vec_exts.reserve(reserve_size);
#if !defined(_MSC_VER) || _MSC_VER >= 1700
	map_exts.reserve(reserve_size);
#endif

	// Table of pointers to tables.
	// This reduces duplication by only requiring a single loop.
	static const RomDataFns *const romDataFns_tbl[] = {
		romDataFns_magic,
		romDataFns_header,
		romDataFns_footer,
		nullptr
	};

	for (const RomDataFns *const *tblptr = &romDataFns_tbl[0];
	     *tblptr != nullptr; tblptr++)
	{
		const RomDataFns *fns = *tblptr;
		for (; fns->supportedFileExtensions != nullptr; fns++) {
			const char *const *sys_exts = fns->supportedFileExtensions();
			if (!sys_exts)
				continue;

			for (; *sys_exts != nullptr; sys_exts++) {
				auto iter = map_exts.find(*sys_exts);
				if (iter != map_exts.end()) {
					// We already had this extension.
					// Update its attributes.
					iter->second |= fns->attrs;
				} else {
					// First time encountering this extension.
					map_exts[*sys_exts] = fns->attrs;
					vec_exts.push_back(RomDataFactory::ExtInfo(
						*sys_exts, fns->attrs));
				}
			}
		}
	}

	// Make sure the vector's attributes fields are up to date.
	for (auto iter = vec_exts.begin(); iter != vec_exts.end(); ++iter) {
		iter->attrs = map_exts[iter->ext];
	}
}

/**
 * Get all supported file extensions.
 * Used for Win32 COM registration.
 *
 * NOTE: The return value is a struct that includes a flag
 * indicating if the file type handler supports thumbnails
 * and/or may have "dangerous" permissions.
 *
 * @return All supported file extensions, including the leading dot.
 */
const vector<RomDataFactory::ExtInfo> &RomDataFactory::supportedFileExtensions(void)
{
	pthread_once(&RomDataFactoryPrivate::once_exts, RomDataFactoryPrivate::init_supportedFileExtensions);
	return RomDataFactoryPrivate::vec_exts;
}

/**
 * Initialize the vector of supported MIME types.
 * Used for KFileMetaData.
 *
 * Internal function; must be called using pthread_once().
 */
void RomDataFactoryPrivate::init_supportedMimeTypes(void)
{
	// TODO: Add generic types, e.g. application/octet-stream?

	// In order to handle multiple RomData subclasses
	// that support the same MIME types, we're using
	// an unordered_set<string>. The actual data
	// is stored in the vector<const char*>.
	unordered_set<string> set_mimeTypes;

	static const size_t reserve_size =
		(ARRAY_SIZE(RomDataFactoryPrivate::romDataFns_header) +
		 ARRAY_SIZE(RomDataFactoryPrivate::romDataFns_footer)) * 2;
	vec_mimeTypes.reserve(reserve_size);
#if !defined(_MSC_VER) || _MSC_VER >= 1700
	set_mimeTypes.reserve(reserve_size);
#endif

	const RomDataFactoryPrivate::RomDataFns *fns =
		&RomDataFactoryPrivate::romDataFns_magic[0];
	for (; fns->supportedFileExtensions != nullptr; fns++) {
		const char *const *sys_mimeTypes = fns->supportedMimeTypes();
		if (!sys_mimeTypes)
			continue;

		for (; *sys_mimeTypes != nullptr; sys_mimeTypes++) {
			auto iter = set_mimeTypes.find(*sys_mimeTypes);
			if (iter == set_mimeTypes.end()) {
				set_mimeTypes.insert(*sys_mimeTypes);
				vec_mimeTypes.push_back(*sys_mimeTypes);
			}
		}
	}

	fns = &RomDataFactoryPrivate::romDataFns_header[0];
	for (; fns->supportedFileExtensions != nullptr; fns++) {
		const char *const *sys_mimeTypes = fns->supportedMimeTypes();
		if (!sys_mimeTypes)
			continue;

		for (; *sys_mimeTypes != nullptr; sys_mimeTypes++) {
			auto iter = set_mimeTypes.find(*sys_mimeTypes);
			if (iter == set_mimeTypes.end()) {
				set_mimeTypes.insert(*sys_mimeTypes);
				vec_mimeTypes.push_back(*sys_mimeTypes);
			}
		}
	}

	fns = &RomDataFactoryPrivate::romDataFns_footer[0];
	for (; fns->supportedFileExtensions != nullptr; fns++) {
		const char *const *sys_mimeTypes = fns->supportedMimeTypes();
		if (!sys_mimeTypes)
			continue;

		for (; *sys_mimeTypes != nullptr; sys_mimeTypes++) {
			auto iter = set_mimeTypes.find(*sys_mimeTypes);
			if (iter == set_mimeTypes.end()) {
				set_mimeTypes.insert(*sys_mimeTypes);
				vec_mimeTypes.push_back(*sys_mimeTypes);
			}
		}
	}
}

/**
 * Get all supported MIME types.
 * Used for KFileMetaData.
 *
 * @return All supported MIME types.
 */
const vector<const char*> &RomDataFactory::supportedMimeTypes(void)
{
	pthread_once(&RomDataFactoryPrivate::once_mimeTypes, RomDataFactoryPrivate::init_supportedMimeTypes);
	return RomDataFactoryPrivate::vec_mimeTypes;
}

}
