/*
 * fwdefs.h
 *
 *  Created on: 15 May 2018
 *      Author: mikee47
 *
 * Firmware update definitions shared between client and device code.
 */

#pragma once

#include <stdint.h>
#include <crypto-aes/GCM.h>

//
#define FWPACK_MAGIC 0xFE1951FA

/*
 * This header is contrived such that every time it is created it will be different,
 * even for the same input.
 *
 * Note that this structure is used on different platforms (Windows/Linux/ESP8266).
 * Default alignment of 4 byte is fine.
 */
struct firmware_header_t {
	// Un-encrypted but authenticated
	struct {
		// magic = FWPACK_MAGIC ^ iv.u32[0]
		uint32_t magic;
		// Encryption initialisation vector
		crypt_iv_t iv;
	} auth;

	// Computed on preceding header bytes
	ghash_token_t hmac;
	// GCM payload tag (HMAC for payload)
	ghash_token_t tag;

	// Last part of header is encrypted
	struct {
		// Firmware version
		uint32_t version;
		// Firmware size
		uint32_t imageSize;
	} encrypted;
};

/*
 * The start of encryption and decryption are similar, but host and embedded code deal with
 * decryption in a different way.
 *
 * header must have imageSize and version fields completed.
 */
static inline void beginEncrypt(GCM& gcm, firmware_header_t& header, const crypt_key_t& devkey)
{
	header.auth.magic = FWPACK_MAGIC ^ header.auth.iv.u32[0];
	// Encrypt source data
	gcm.clear();
	gcm.setKey(devkey);
	gcm.setIV(header.auth.iv);
	gcm.addAuthData(&header.auth, sizeof(header.auth));
	gcm.encrypt(&header.encrypted, sizeof(header.encrypted));
	header.hmac = gcm.computeTag();
	// Re-initialise GCM to encrypt/decrypt payload
	gcm.clear();
	gcm.setIV(header.hmac.iv);
}

static inline bool beginDecrypt(GCM& gcm, firmware_header_t& header, const crypt_key_t& devkey)
{
	if((header.auth.magic ^ header.auth.iv.u32[0]) != FWPACK_MAGIC) {
		return false;
	}
	gcm.clear();
	gcm.setKey(devkey);
	gcm.setIV(header.auth.iv);
	gcm.addAuthData(&header.auth, sizeof(header.auth));
	gcm.decrypt(&header.encrypted, sizeof(header.encrypted));
	if(header.hmac != gcm.computeTag()) {
		return false;
	}

	// Re-initialise GCM to encrypt/decrypt payload
	gcm.clear();
	gcm.setIV(header.hmac.iv);
	return true;
}
