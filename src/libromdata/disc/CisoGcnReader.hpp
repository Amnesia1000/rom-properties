/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * CisoGcnReader.hpp: GameCube/Wii CISO disc image reader.                 *
 *                                                                         *
 * Copyright (c) 2016 by David Korth.                                      *
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
 * You should have received a copy of the GNU General Public License along *
 * with this program; if not, write to the Free Software Foundation, Inc., *
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.           *
 ***************************************************************************/

#ifndef __ROMPROPERTIES_LIBROMDATA_DISC_CISOGCNREADER_HPP__
#define __ROMPROPERTIES_LIBROMDATA_DISC_CISOGCNREADER_HPP__

#include "IDiscReader.hpp"

namespace LibRomData {

class CisoGcnReaderPrivate;
class CisoGcnReader : public IDiscReader
{
	public:
		/**
		 * Construct a CisoGcnReader with the specified file.
		 * The file is dup()'d, so the original file can be
		 * closed afterwards.
		 * @param file File to read from.
		 */
		explicit CisoGcnReader(IRpFile *file);
		virtual ~CisoGcnReader();

	private:
		typedef IDiscReader super;
		RP_DISABLE_COPY(CisoGcnReader)

	private:
		friend class CisoGcnReaderPrivate;
		CisoGcnReaderPrivate *const d;

	public:
		/** Disc image detection functions. **/

		/**
		 * Is a disc image supported by this class?
		 * @param pHeader Disc image header.
		 * @param szHeader Size of header.
		 * @return Class-specific disc format ID (>= 0) if supported; -1 if not.
		 */
		static int isDiscSupported_static(const uint8_t *pHeader, size_t szHeader);

		/**
		 * Is a disc image supported by this object?
		 * @param pHeader Disc image header.
		 * @param szHeader Size of header.
		 * @return Class-specific disc format ID (>= 0) if supported; -1 if not.
		 */
		virtual int isDiscSupported(const uint8_t *pHeader, size_t szHeader) const final;

	public:
		/**
		 * Is the disc image open?
		 * This usually only returns false if an error occurred.
		 * @return True if the disc image is open; false if it isn't.
		 */
		virtual bool isOpen(void) const final;

		/**
		 * Read data from the disc image.
		 * @param ptr Output data buffer.
		 * @param size Amount of data to read, in bytes.
		 * @return Number of bytes read.
		 */
		virtual size_t read(void *ptr, size_t size) final;

		/**
		 * Set the disc image position.
		 * @param pos Disc image position.
		 * @return 0 on success; -1 on error.
		 */
		virtual int seek(int64_t pos) final;

		/**
		 * Seek to the beginning of the disc image.
		 */
		virtual void rewind(void) final;

		/**
		 * Get the disc image size.
		 * @return Disc image size, or -1 on error.
		 */
		virtual int64_t size(void) final;
};

}

#endif /* __ROMPROPERTIES_LIBROMDATA_DISC_CISOGCNREADER_HPP__ */
