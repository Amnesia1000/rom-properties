/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * KeyManager.hpp: Encryption key manager.                                 *
 *                                                                         *
 * Copyright (c) 2016-2017 by David Korth.                                 *
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

#ifndef __ROMPROPERTIES_LIBROMDATA_CRYPTO_KEYMANAGER_HPP__
#define __ROMPROPERTIES_LIBROMDATA_CRYPTO_KEYMANAGER_HPP__

#include "common.h"

// C includes.
#include <stdint.h>

namespace LibRomData {

class KeyManagerPrivate;
class KeyManager
{
	protected:
		/**
		 * KeyManager class.
		 *
		 * This class is a Singleton, so the caller must obtain a
		 * pointer to the class using instance().
		 */
		KeyManager();
		~KeyManager();

	private:
		RP_DISABLE_COPY(KeyManager)

	private:
		friend class KeyManagerPrivate;
		KeyManagerPrivate *const d_ptr;

	public:
		/**
		 * Get the KeyManager instance.
		 * @return KeyManager instance.
		 */
		static KeyManager *instance(void);

	public:
		// Encryption key data.
		struct KeyData_t {
			const uint8_t *key;	// Key data.
			uint32_t length;	// Key length.
		};

		/**
		 * Have the encryption keys been loaded yet?
		 *
		 * This function will *not* load the keys.
		 * To load the keys, call get() with the requested key name.
		 *
		 * If this function returns false after calling get(),
		 * keys.conf is probably missing.
		 *
		 * @return True if keys have been loaded; false if not.
		 */
		bool areKeysLoaded(void) const;

		/**
		 * Reload keys if the key configuration file has changed.
		 * @return 0 on success; negative POSIX error code on error.
		 */
		int reloadIfChanged(void);

		/**
		 * Key verification result.
		 */
		enum VerifyResult {
			VERIFY_OK			= 0,	// Key obtained/verified.
			VERIFY_INVALID_PARAMS		= 1,	// Parameters are invalid.
			VERIFY_KEY_DB_NOT_LOADED	= 2,	// Key database is not loaded.
			VERIFY_KEY_DB_ERROR		= 3,	// Something's wrong with the key database.
			VERIFY_KEY_NOT_FOUND		= 4,	// Key was not found.
			VERIFY_KEY_INVALID		= 5,	// Key is not valid for this operation.
			VERFIY_IAESCIPHER_INIT_ERR	= 6,	// IAesCipher could not be created.
			VERIFY_IAESCIPHER_DECRYPT_ERR	= 7,	// IAesCipher::decrypt() failed.
			VERIFY_WRONG_KEY		= 8,	// The key did not decrypt the test string correctly.
		};

		/**
		 * Get an encryption key.
		 * @param keyName	[in]  Encryption key name.
		 * @param pKeyData	[out,opt] Key data struct. (If nullptr, key will be checked but not loaded.)
		 * @return VerifyResult.
		 */
		VerifyResult get(const char *keyName, KeyData_t *pKeyData) const;

		/**
		 * Verify and retrieve an encryption key.
		 *
		 * This will decrypt the specified block of data
		 * using the key with AES-128-ECB, which will result
		 * in the 16-byte string "AES-128-ECB-TEST".
		 *
		 * If the key is valid, pKeyData will be populated
		 * with the key information, similar to get().
		 *
		 * @param keyName	[in] Encryption key name.
		 * @param pKeyData	[out,opt] Key data struct. (If nullptr, key will be checked but not loaded.)
		 * @param pVerifyData	[in] Verification data block.
		 * @param verifyLen	[in] Length of pVerifyData. (Must be 16.)
		 * @return VerifyResult.
		 */
		VerifyResult getAndVerify(const char *keyName, KeyData_t *pKeyData,
			const uint8_t *pVerifyData, unsigned int verifyLen) const;
};

}

#endif /* __ROMPROPERTIES_LIBROMDATA_CRYPTO_KEYMANAGER_HPP__ */
