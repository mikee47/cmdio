/*
 * access.h
 *
 *  Created on: 6 Jun 2018
 *      Author: Mike
 */

#ifndef __ACCESS_H
#define __ACCESS_H

#include <WString.h>


// Access Control level
#define ACCESS_TYPE_MAP(XX) \
  XX(none,    Unauthenticated) \
  XX(guest,   Guest) \
  XX(user,    Authenticated) \
  XX(admin,   Administrative)

enum __attribute__((packed)) access_type_t {
#define XX(_tag, _comment) access_##_tag,
  ACCESS_TYPE_MAP(XX)
#undef XX
};

String accessTypeToStr(access_type_t access);
access_type_t getAccessType(const char* str);


#endif // __ACCESS_H
