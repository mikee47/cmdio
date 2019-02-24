/*
 * CommandHandler.h
 *
 *  Created on: 5 Jun 2018
 *      Author: mikee47
 */

#ifndef __CMDHANDLER_H
#define __CMDHANDLER_H

#include "Libraries/ArduinoJson/ArduinoJson.h"
#include "IPAddress.h"
#include "IFS/Access.h"
#include "status.h"
#include "CommandConnection.h"

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

	virtual String getMethod() const = 0;

	/*
	 * Get access type required for this command.
	 *
	 * @param command Specify NULL to get minimum access for this handler.
	 */
	virtual UserRole minAccess() const
	{
		// By default, require maximum access.
		return UserRole::Admin;
	}

	virtual void handleMessage(WSCommandConnection* connection, JsonObject& json)
	{
		setError(json, ioe_bad_command);
	}

	// Return true if data consumed
	virtual bool handleData(WSCommandConnection* connection, uint8_t* data, size_t size)
	{
		return false;
	}

	bool operator==(const WSCommandHandler& handler) const
	{
		return this == &handler;
	}
};

#endif // __CMDHANDLER_H
