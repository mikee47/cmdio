/*
 * CommandHandler.h
 *
 *  Created on: 5 Jun 2018
 *      Author: mikee47
 */

#pragma once

#include <ArduinoJson.h>
#include <IpAddress.h>
#include <IFS/File/Access.h>
#include <IO/Error.h>
#include "CommandConnection.h"

using UserRole = IFS::UserRole;

// Tag used in messages to identify method; responses must contain the same method
DECLARE_FSTR(ATTR_METHOD)
DECLARE_FSTR(ATTR_COMMAND)
DECLARE_FSTR(COMMAND_INFO)
DECLARE_FSTR(ATTR_NAME)
DECLARE_FSTR(DONT_RESPOND)

class WSCommandHandler
{
public:
	virtual ~WSCommandHandler()
	{
	}

	/**
	 * @brief Get tag identifying methods this handler deals with
	 * @retval String
	 */
	virtual String getMethod() const = 0;

	/**
	 * @brief Get access type required for this command.
	 * @retval UserRole
	 */
	virtual UserRole getMinAccess() const
	{
		// By default, require maximum access.
		return UserRole::Admin;
	}

	/**
	 * @brief Handle a JSON message/command
	 * @param connection If specified, identifes connection for reply
	 * @param json
	 */
	virtual void handleMessage(WSCommandConnection* connection, JsonObject json)
	{
		IO::setError(json, IO::Error::bad_command);
	}

	/**
	 * @brief Handle a binary message
	 * @param connection If specified, identifes connection for reply
	 * @param data
	 * @param size
	 * @retval bool True if data consumed, false on error e.g. message unexpected
	 */
	virtual bool handleData(WSCommandConnection* connection, uint8_t* data, size_t size)
	{
		return false;
	}

	bool operator==(const WSCommandHandler& handler) const
	{
		return this == &handler;
	}
};
