/*
 * fwpack.cpp
 *
 *  Created on: 15 May 2018
 *      Author: mikee47
 *
 * Firmware (or other content) packaging utility.
 * Provides the follow functions:
 *
 *  1.  Generate device key from passphrase. This key is embedded in device and used to
 *      decrypt firmware images. It could also be used to encrypt responses. Passphrase
 *      could be the device MAC address plus allocated serial number.
 *  2.  Encrypt and sign content using device key:
 *        - Generate random IV
 *        - Sign header
 *        - Encrypt & sign payload
 *
 * Header is unencrypted and contains:
 *  - randomly generated IV
 *  - content version
 *  - content size
 *
 * If header fails authentication we abort the update and close the network connection.
 * HMAC for the payload appears at the end.
 * The version field can be used by firmware to control behaviour, for example by failing
 * if the version is not greater than the current one.
 *
 */

#include "fwpack.h"
#include <SmingCore.h>
#include <Data/HexString.h>
#include <hostlib/hostmsg.h>
#include <hostlib/CommandLine.h>

/**
 * @brief Create a key based on a master key and pass phrase
 * @param masterKey Master key data, must be 16 bytes exactly
 * @param devkey [OUT] The generated key, should be embedded in device
 * @param pass A text passphrase, specific to the device
 * @retval bool true on success
 */
bool createDeviceKey(const String& masterKey, crypt_key_t& devkey, const String& pass)
{
	ghash_token_t master_key;
	if(masterKey.length() != sizeof(master_key)) {
		host_printf("Invalid master key\r\n");
		return false;
	}

	memcpy(&master_key, masterKey.c_str(), sizeof(master_key));

	// Hash is a one-way function
	GHASH ghash;
	ghash.reset(master_key);
	ghash.update(pass.c_str(), pass.length());
	devkey = ghash.finalize();

	//  debug_hex(INFO, "key", master_key, sizeof(master_key));
	//  debug_i("Passphrase: %s", pass.c_str());
	//  debug_hex(INFO, "Device Key", devkey.u8, sizeof(devkey));

	return true;
}

bool saveToFile(const String& filename, const void* header, size_t headerSize, const void* content, size_t contentSize)
{
	auto& fs = IFS::Host::getFileSystem();
	IFS::File file(&fs);
	if(!file.open(filename, File::CreateNewAlways | File::WriteOnly)) {
		host_printf("Failed to create '%s'\r\n", filename.c_str());
		return false;
	}
	if(header != nullptr) {
		file.write(header, headerSize);
	}
	if(content != nullptr) {
		file.write(content, contentSize);
	}

	return true;
}

bool encrypt(String& data, const String& destFile, const crypt_key_t& devkey, const String& versionString)
{
	char* tailptr = nullptr;
	uint32_t version = strtoul(versionString.c_str(), &tailptr, 16);
	if(*tailptr != '\0') {
		host_printf("Invalid version '%s' - hex number expected\n", versionString.c_str());
		return false;
	}

	// Generate header
	firmware_header_t header = {};
	header.encrypted.version = version;
	header.encrypted.imageSize = data.length();

	// Generate a random IV
	os_get_random(header.auth.iv.u8, sizeof(header.auth.iv));

	GCM gcm;
	beginEncrypt(gcm, header, devkey);
	gcm.encrypt(data.begin(), data.length());
	header.tag = gcm.computeTag();

	debug_hex(DBG, "Device Key", devkey.u8, sizeof(devkey));
	debug_hex(DBG, "IV ", header.auth.iv.u8, sizeof(header.auth.iv));
	debug_hex(DBG, "HMAC", header.hmac.u8, sizeof(header.hmac));
	debug_hex(DBG, "Tag", header.tag.u8, sizeof(header.tag));

	// We now have header, ciphertext and tag for transport
	// payload = header + ciphertext + tag

	if(saveToFile(destFile, &header, sizeof(header), data.c_str(), data.length())) {
		host_printf("Payload '%s' created, version 0x%08x\n", destFile.c_str(), version);
		host_printf("Device Key: %s\n", makeHexString(devkey.u8, sizeof(devkey), ',').c_str());
	}

	return true;
}

bool decrypt(String& data, const String& destFile, const crypt_key_t& devkey)
{
	if(data.length() < sizeof(firmware_header_t)) {
		return false; // Should have already been checked
	}

	auto& header = *reinterpret_cast<firmware_header_t*>(data.begin());

	GCM gcm;
	if(!beginDecrypt(gcm, header, devkey)) {
		host_printf("Header authentication failed\n");
		return false;
	}

	debug_hex(DBG, "Device Key", devkey.u8, sizeof(devkey));
	debug_hex(DBG, "IV ", header.auth.iv.u8, sizeof(header.auth.iv));
	debug_hex(DBG, "Tag", header.tag.u8, sizeof(header.tag));
	debug_hex(DBG, "HMAC", header.hmac.u8, sizeof(header.hmac));

	host_printf("Source size = %u, image size = %u\n", data.length(), header.encrypted.imageSize);

	if(data.length() != sizeof(firmware_header_t) + header.encrypted.imageSize) {
		host_printf("Payload size incorrect");
		return false;
	}

	char* payload = data.begin() + sizeof(firmware_header_t);
	gcm.decrypt(payload, header.encrypted.imageSize);

	if(header.tag != gcm.computeTag()) {
		host_printf("Tag FAIL");
		return false;
	}

	bool res = saveToFile(destFile, nullptr, 0, payload, header.encrypted.imageSize);
	if(res) {
		host_printf("Firmware version 0x%08x decrypted to '%s'\n", header.encrypted.version, destFile.c_str());
	}
	return res;
}

static void usage()
{
	host_printf("Usage: fwpack [source] [destination] [master key file] [passphrase] [version]\r\n"
				"  Pack:\n"
				"    fwpack [source] [destination] [master key file] [passphrase] [version]\n"
				"  Unpack:\n"
				"    fwpack [source] [destination] [master key file] [passphrase]\n"
				"  Detect:\n"
				"    fwpack [source]\n"
				"  Create device key file:\n"
				"    fwpack - [destination] [master key file] [passphrase]\n"
				"\r\n");
}

bool run()
{
	auto& params = commandLine.getParameters();

	if(params.count() == 0) {
		usage();
		return false;
	}

	m_setPuts(host_nputs);

	String sourceFile = params[0].getValue();
	bool createKeyFile = (sourceFile[0] == '-');

	auto& fs = IFS::Host::getFileSystem();

	String source;

	if(!createKeyFile) {
		// We can detect whether we're encrypting or not using magic field
		source = fs.getContent(sourceFile);
		if(source.length() == 0) {
			host_printf("'%s' is empty", sourceFile.c_str());
			return false;
		}
	}

	// Detect source data type
	bool isPackage = false;
	if(source.length() >= sizeof(firmware_header_t)) {
		auto header = reinterpret_cast<const firmware_header_t*>(source.c_str());
		if((header->auth.magic ^ header->auth.iv.u32[0]) == FWPACK_MAGIC) {
			isPackage = true;
		}
	}

	if(params.count() < 2) {
		host_printf("'%s' is %s\n", sourceFile.c_str(), isPackage ? "a package" : "not a package");
		return true;
	}

	unsigned paramCount = (isPackage || createKeyFile) ? 4 : 5;
	if(params.count() != paramCount) {
		usage();
		return false;
	}

	String destFile = params[1].getValue();
	String masterKey = fs.getContent(params[2].getValue());
	String pass = params[3].getValue();

	crypt_key_t devkey;
	if(!createDeviceKey(masterKey, devkey, pass)) {
		return false;
	}
	if(createKeyFile) {
		if(!saveToFile(destFile, &devkey, sizeof(devkey), nullptr, 0)) {
			return false;
		}
		host_printf("Device key saved to '%s'\n", destFile.c_str());
		return true;
	}

	if(isPackage) {
		return decrypt(source, destFile, devkey);
	}

	return encrypt(source, destFile, devkey, params[4].getValue());
}

void init()
{
	run();
	system_restart();
}
