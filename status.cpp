/*
 * status.cpp
 *
 *  Created on: 28 May 2018
 *      Author: Mike
 */

#include <SmingCore/SmingCore.h>
#include <status.h>


static DEFINE_STRING_P(ATTR_STATUS,     "status")
static DEFINE_STRING_P(STATUS_SUCCESS,  "success")
static DEFINE_STRING_P(STATUS_PENDING,  "pending")
static DEFINE_STRING_P(STATUS_ERROR,    "error")
static DEFINE_STRING_P(STATUS_UNKNOWN,  "unknown")

// Json error node
static DEFINE_STRING_P(ATTR_ERROR, "error")
DEFINE_STRING_P(ATTR_CODE, "code")
DEFINE_STRING_P(ATTR_TEXT, "text")
static DEFINE_STRING_P(ATTR_ARG, "arg")

String statusToStr(request_status_t status)
{
  switch (status) {
  case status_success:
    return STATUS_SUCCESS();
  case status_pending:
    return STATUS_PENDING();
  case status_error:
    return STATUS_ERROR();
  default:
    return STATUS_UNKNOWN();
  }
}


void setStatus(JsonObject& json, request_status_t status)
{
  json[ATTR_STATUS()] = statusToStr(status);
}

void setError(JsonObject& json, int code, const String& text, const String& arg)
{
  setStatus(json, status_error);
  JsonObject& err = json.createNestedObject(ATTR_ERROR());
  err[ATTR_CODE()] = code;
  if (text.length())
    err[ATTR_TEXT()] = text;
  if (arg.length())
    err[ATTR_ARG()] = arg;
}


#define XX(_tag, _comment) #_comment "\0"
DEFINE_SZSTRING_P(IO_ERROR_TEXT, IOERROR_MAP(XX))
#undef XX

ioerror_t setError(JsonObject& json, ioerror_t err, const String& arg)
{
  setError(json, err, IO_ERROR_TEXT().szGetText(err), arg);
  return err;
}


