/***************************************************************************
 * ROM Properties Page shell extension. (KDE)                              *
 * RpExtractorPlugin.cpp: KFileMetaData forwarder.                         *
 *                                                                         *
 * Qt's plugin system prevents a single shared library from exporting      *
 * multiple plugins, so this file acts as a KFileMetaData ExtractorPlugin, *
 * and then forwards the request to the main library.                      *
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

#include "RpExtractorPlugin.hpp"
#include "RpQt.hpp"

// librpbase
#include "librpbase/RomData.hpp"
#include "librpbase/RomMetaData.hpp"
#include "librpbase/file/RpFile.hpp"
using LibRpBase::RomData;
using LibRpBase::RomMetaData;
using LibRpBase::IRpFile;
using LibRpBase::RpFile;

// libromdata
#include "libromdata/RomDataFactory.hpp"
using LibRomData::RomDataFactory;

// C includes. (C++ namespace)
#include <cassert>

// C++ includes.
#include <memory>
#include <string>
#include <vector>
using std::string;
using std::unique_ptr;
using std::vector;

// Qt includes.
#include <QtCore/QDateTime>

// KDE includes.
#include <kfilemetadata/extractorplugin.h>
#include <kfilemetadata/properties.h>
using KFileMetaData::ExtractorPlugin;
using KFileMetaData::ExtractionResult;
using namespace KFileMetaData::Property;

#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)

#include <kfileitem.h>

namespace RomPropertiesKDE {

/**
 * Factory method.
 * NOTE: Unlike the ThumbCreator version, this one is specific to
 * rom-properties, and is called by a forwarder library.
 */
extern "C" {
	Q_DECL_EXPORT RpExtractorPlugin *PFN_CREATEEXTRACTORPLUGINKDE_FN(QObject *parent)
	{
		return new RpExtractorPlugin(parent);
	}
}

RpExtractorPlugin::RpExtractorPlugin(QObject *parent)
	: super(parent)
{ }

QStringList RpExtractorPlugin::mimetypes(void) const
{
	// Get the MIME types from RomDataFactory.
	const vector<const char*> &vec_mimeTypes = RomDataFactory::supportedMimeTypes();

	// Convert to QStringList.
	QStringList mimeTypes;
	mimeTypes.reserve(static_cast<int>(vec_mimeTypes.size()));
	for (auto iter = vec_mimeTypes.cbegin(); iter != vec_mimeTypes.cend(); ++iter) {
		mimeTypes += QString::fromUtf8(*iter);
	}
	return mimeTypes;
}

void RpExtractorPlugin::extract(ExtractionResult *result)
{
	// TODO: Check if the input URL has a scheme.
	// In testing, it seems to only be local paths.
	QString filename = result->inputUrl();
	if (filename.isEmpty())
		return;

	// Single file, and it's local.
	// TODO: RpQFile wrapper.
	// For now, using RpFile, which is an stdio wrapper.
	unique_ptr<RpFile> file(new RpFile(Q2U8(filename), RpFile::FM_OPEN_READ_GZ));
	if (!file || !file->isOpen()) {
		// Could not open the file.
		return;
	}

	// Get the appropriate RomData class for this ROM.
	// RomData class *must* support at least one image type.
	RomData *const romData = RomDataFactory::create(file.get());
	file.reset(nullptr);	// file is dup()'d by RomData.
	if (!romData) {
		// ROM is not supported.
		return;
	}

	// Get the metadata properties.
	const RomMetaData *metaData = romData->metaData();
	if (!metaData || metaData->empty()) {
		// No metadata properties.
		romData->unref();
		return;
	}

	// Process the metadata.
	const int count = metaData->count();
	for (int i = 0; i < count; i++) {
		const RomMetaData::MetaData *prop = metaData->prop(i);
		assert(prop != nullptr);
		if (!prop)
			continue;

		// RomMetaData's property indexes match KFileMetaData.
		// No conversion is necessary.
		switch (prop->type) {
			case LibRpBase::PropertyType::Integer: {
				int ivalue = prop->data.ivalue;
				if (prop->name == LibRpBase::Property::Duration) {
					// Duration needs to be converted from ms to seconds.
					ivalue /= 1000;
				}
				result->add(static_cast<KFileMetaData::Property::Property>(prop->name), ivalue);
				break;
			}

			case LibRpBase::PropertyType::UnsignedInteger: {
				result->add(static_cast<KFileMetaData::Property::Property>(prop->name),
					    prop->data.uvalue);
				break;
			}

			case LibRpBase::PropertyType::String: {
				const string *str = prop->data.str;
				result->add(static_cast<KFileMetaData::Property::Property>(prop->name),
					QString::fromUtf8(str->data(), static_cast<int>(str->size())));
				break;
			}

			case LibRpBase::PropertyType::Timestamp: {
				// TODO: Verify timezone handling.
				// NOTE: fromMSecsSinceEpoch() with TZ spec was added in Qt 5.2.
				// Maybe write a wrapper function? (RomDataView uses this, too.)
				// NOTE: Some properties might need the full QDateTime.
				// CreationDate seems to work fine with just QDate.
				QDateTime dateTime;
				dateTime.setTimeSpec(Qt::UTC);
				dateTime.setMSecsSinceEpoch((qint64)prop->data.timestamp * 1000);
				result->add(static_cast<KFileMetaData::Property::Property>(prop->name),
					dateTime.date());
				break;
			}

			default:
				// ERROR!
				assert(!"Unsupported RomMetaData PropertyType.");
				break;
		}
	}

	// Finished extracting metadata.
	romData->unref();
}

}

#endif /* QT_VERSION >= QT_VERSION_CHECK(5,0,0) */
