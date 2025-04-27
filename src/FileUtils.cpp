/*
 * FileUtils.cpp
 *
 *  Created on: 28 May 2018
 *      Author: mikee47
 */

#include "include/cmdio/FileManager.h"

namespace FileUtils
{
DEFINE_FSTR(METHOD_FILES, "files")
DEFINE_FSTR(COMMAND_UPLOAD, "upload")
DEFINE_FSTR(ATTR_ACCESS, "access")

// getFileInfo()
DEFINE_FSTR(ATTR_SIZE, "size")
DEFINE_FSTR_LOCAL(ATTR_ATTR, "attr")
DEFINE_FSTR_LOCAL(ATTR_MTIME, "mtime")

#define UPLOAD_TIMEOUT_MS 2000

bool IsValidUtf8(const char* str, unsigned length)
{
	if(str == nullptr) {
		return true;
	}

	unsigned i = 0;
	while(i < length) {
		char c = str[i++];
		if((c & 0x80) == 0) {
			continue;
		}

		if(i >= length) {
			return false; // incomplete multibyte char
		}

		if(c & 0x20) {
			c = str[i++];
			if((c & 0xC0) != 0x80) {
				return false; // malformed trail byte or out of range char
			}
			if(i >= length) {
				return false; // incomplete multibyte char
			}
		}

		c = str[i++];
		if((c & 0xC0) != 0x80) {
			return false; // malformed trail byte
		}
	}

	return true;
}

// Check for invalid characters and replace them - can break browser operation otherwise
char* checkString(char* str, unsigned length)
{
	if(!IsValidUtf8(str, length)) {
		debug_w("Invalid UTF8: %s", str);
		for(unsigned i = 0; i < length; ++i) {
			char& c = str[i];
			if(c < 0x20 || c > 127)
				c = '_';
		}
	}
	return str;
}

void getFileInfo(JsonObject json, const FileStat& stat)
{
	String s(stat.name.buffer, stat.name.length);
	checkString(s.begin(), s.length());
	String attrName = ATTR_NAME;
	if(!json.containsKey(attrName)) {
		json[attrName] = s;
	}
	json[ATTR_SIZE] = stat.size;
	IFS::IFileSystem::Info fsi;
	stat.fs->getinfo(fsi);
	json["fs"] = (int)fsi.type;
	json[ATTR_ACCESS] = IFS::getAclString(stat.acl);
	json[ATTR_ATTR] = IFS::getFileAttributeString(stat.attr);
	json[ATTR_MTIME] = DateTime(SystemClock.now()).toISO8601();
}

void getFileInfo(JsonObject json, const String& filename)
{
	FileNameStat stat;
	if(fileStats(filename, stat) == FS_OK) {
		getFileInfo(json, stat);
	}
}

String getDirName(const String& filename)
{
	int i = filename.lastIndexOf('/');
	return (i < 0) ? nullptr : filename.substring(0, i);
}

} // namespace FileUtils
