#include "include/cmdio/ActionHandler.h"
#include <IO/DeviceManager.h>
#include <IO/Strings.h>
#include "include/cmdio/WebsocketManager.h"

DEFINE_FSTR_LOCAL(METHOD_ACTION, "actions")
DEFINE_FSTR_LOCAL(FILE_ACTION_CONFIG, "config/actions.json")
DEFINE_FSTR_LOCAL(ATTR_ITEMS, "items")
DEFINE_FSTR(COMMAND_TRIGGER, "trigger")

#define REQUEST_INTERVAL 500

using namespace IO;

void ActionHandler::trigger(const CStringArray& actions)
{
	addRequests(actions);

	if(requestCount == 0) {
		executeRequest();
	}

	StaticJsonDocument<512> doc;
	auto json = doc.to<JsonObject>();
	json[ATTR_METHOD] = METHOD_ACTION;
	json[ATTR_COMMAND] = COMMAND_TRIGGER;
	IO::setPending(json);
	socketManager.broadcast(json);
}

void ActionHandler::addRequests(const CStringArray& actions)
{
	DynamicJsonDocument doc(4096);
	FileStream input(FILE_ACTION_CONFIG);
	input.setTimeout(0);
	input.find("{");
	do {
		input.find("\"");
		String id = input.readStringUntil('"');
		input.find(":");
		Json::deserialize(doc, input);
		debug_i("ACTION '%s': %u bytes", id.c_str(), doc.memoryUsage());
		if(!actions.contains(id)) {
			continue;
		}
		auto action = doc.as<JsonObject>();
		JsonArray items = action[ATTR_ITEMS];
		for(JsonObjectConst item : items) {
			requestQueue += Json::serialize(item);
		}
	} while(input.findUntil(",", "]"));
}

void ActionHandler::executeRequest()
{
	String reqStr;
	while((reqStr = requestQueue.popFront())) {
		StaticJsonDocument<512> doc;
		Json::deserialize(doc, reqStr.begin());
		debug_i("[ACT] Request JSON %u bytes", doc.memoryUsage());
		auto obj = doc.as<JsonObject>();

		IO::Request* req;
		if(!check(IO::devmgr.createRequest(obj[IO::FS_device], req))) {
			continue;
		}

		if(!check(req->parseJson(obj))) {
			delete req;
			continue;
		}

		req->onComplete([this](const IO::Request& req) {
			auto err = req.error();
			if(err) {
				lastError = err;
				++errorCount;
			}
			timer.initializeMs<REQUEST_INTERVAL>([this]() { executeRequest(); });
			timer.startOnce();
		});
		req->submit();
		++requestCount;
		return;
	}

	// Defer this as we may be handling an incoming message
	System.queueCallback([err = lastError, reqCount = requestCount, errCount = errorCount]() {
		StaticJsonDocument<512> doc;
		auto obj = doc.to<JsonObject>();
		obj[ATTR_METHOD] = METHOD_ACTION;
		obj[ATTR_COMMAND] = COMMAND_TRIGGER;
		if(errCount == 0) {
			IO::setSuccess(obj);
		} else {
			IO::setError(obj, err, IO::Error::toString(err), String(errCount) + F(" errors"));
		}
		obj["count"] = reqCount;
		socketManager.broadcast(obj);
	});

	requestQueue = nullptr;
	lastError = IO::Error::success;
	requestCount = 0;
	errorCount = 0;

	debug_i("[ACT] All requests complete");
}

bool ActionHandler::check(IO::ErrorCode err)
{
	if(!err) {
		return true;
	}
	lastError = err;
	++errorCount;
	debug_i("[ACT] %s", IO::Error::toString(err).c_str());
	return false;
}

void ActionHandler::handleMessage(WSCommandConnection* connection, JsonObject json)
{
	const char* command = json[ATTR_COMMAND];
	if(COMMAND_TRIGGER == command) {
		auto id = json["id"].as<const char*>();
		trigger(id);
		json[DONT_RESPOND] = true;
		return;
	}

	WSCommandHandler::handleMessage(connection, json);
}

String ActionHandler::getMethod() const
{
	return METHOD_ACTION;
}
