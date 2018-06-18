/*
 * access.cpp
 *
 *  Created on: 6 Jun 2018
 *      Author: Mike
 */


#include "access.h"
#include <WString_P.h>

#define XX(_tag, _comment) #_tag "\0"
static DEFINE_SZSTRING_P(ACCESS_TYPE_TAGS, ACCESS_TYPE_MAP(XX))
#undef XX

String accessTypeToStr(access_type_t access)
{
  String_P tags = ACCESS_TYPE_TAGS();
  const char* ps = tags.szGetText(access);
  return ps ?: tags.szGetText(access_none);
}

access_type_t getAccessType(const char* str)
{
  uint8_t n = access_none;
  ACCESS_TYPE_TAGS().szGetValue(str, n);
  return static_cast<access_type_t>(n);
}

