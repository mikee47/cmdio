/*
 * FileUtils.h - Declarations and utility functions used by more than one unit
 *
 *  Created on: 28 May 2018
 *      Author: mikee47
 */

#pragma once

#include <FileSystem.h>

namespace FileUtils
{
DECLARE_FSTR(METHOD_FILES)
DECLARE_FSTR(COMMAND_UPLOAD)
DECLARE_FSTR(ATTR_SIZE)

bool IsValidUtf8(const char* str, unsigned length);

// Check for invalid characters and replace them - can break browser operation otherwise
char* checkString(char* str, unsigned length);

void getFileInfo(JsonObject json, const FileStat& stat);

void getFileInfo(JsonObject json, File& file);

} // namespace FileUtils

DECLARE_FSTR(ATTR_ACCESS)
