#include "include/cmdio/RequestHandler.h"
#include <IO/DeviceManager.h>

DEFINE_FSTR_LOCAL(METHOD_IOCONTROL, "iocontrol")

using namespace IO;

void RequestHandler::handleMessage(WSCommandConnection* connection, JsonObject json)
{
	ErrorCode err = devmgr.handleMessage(json, [connection](const Request& request) {
		debug_i("RequestHandler::handleMessage(%p, %s)", connection, request.id().c_str());
		StaticJsonDocument<2048> doc;
		auto json = doc.to<JsonObject>();
		request.getJson(json);
		json[ATTR_METHOD] = METHOD_IOCONTROL;
		connection->send(json);
	});

	if(err) {
		return;
	}

	// Response will be sent when command starts executing
	json[DONT_RESPOND] = true;
}

String RequestHandler::getMethod() const
{
	return METHOD_IOCONTROL;
}
