#include "ModbusHandler.h"
#include <IO/DeviceManager.h>
#include <IO/Modbus/GenericRequest.h>
#include <IO/Strings.h>

using namespace IO;

namespace
{
DEFINE_FSTR(METHOD_MODBUS, "modbus")

constexpr unsigned maxRequestSize{20};

} // namespace

void ModbusHandler::handleMessage(WSCommandConnection* connection, JsonObject json)
{
	/* Forward regular commands to the device manager */

	if(json.containsKey(FS_command)) {
		auto err = devmgr.handleMessage(json, [connection](const Request& request) {
			debug_i("ModbusHandler::handleMessage(%p, %s)", connection, request.id().c_str());
			DynamicJsonDocument doc(2048);
			auto json = doc.to<JsonObject>();
			json[ATTR_METHOD] = METHOD_MODBUS;
			request.getJson(json);
			connection->send(json);
		});

		if(!err) {
			json[DONT_RESPOND] = true;
		}
		return;
	}

	/* Handle generic modbus request */

	if(!devmgr.findDevice(json[FS_device], device) || device->type() != DeviceType::Modbus) {
		setError(json, Error::bad_device);
		return;
	}

	auto callback = [connection](const Request& request) {
		debug_i("ModbusHandler::handleMessage(%p, %s)", connection, request.id().c_str());
		StaticJsonDocument<2048> doc;
		auto json = doc.to<JsonObject>();
		json[ATTR_METHOD] = METHOD_MODBUS;
		request.getJson(json);
		connection->send(json);
	};

	auto req = new Modbus::GenericRequest(*device);
	if(req == nullptr) {
		setError(json, Error::no_mem);
		return;
	}
	auto err = req->parseJson(json);
	if(err) {
		delete req;
		setError(json, err);
		return;
	}
	req->onComplete(callback);
	req->submit();

	// Response will be sent when command starts executing
	json[DONT_RESPOND] = true;
}

bool ModbusHandler::sendNext()
{
	if(remaining == 0) {
		return false;
	}

	auto req = new Modbus::GenericRequest(*device);
	if(req == nullptr) {
		return false;
	}
	req->function = function;
	req->count = std::max(maxRequestSize, remaining);
	req->address = address;
	req->onComplete([&](const Request& request) {
		if(request.error()) {
			dumpFile.println(F("Error: ") + Error::toString(request.error()));
			remaining = 0;
			dumpFile.close();
			return;
		}
		auto& req = static_cast<const Modbus::GenericRequest&>(request);
		auto& rsp = req.pdu->data.readHoldingRegisters.response;
		unsigned n = rsp.getCount();
		String s;
		for(unsigned i = 0; i < n; ++i) {
			if(i != 0) {
				s += ' ';
			}
			char buf[32];
			ultoa_wp(rsp.values[i], buf, 16, 4, '0');
			s += buf;
		}
		dumpFile.println(s);

		address += n;
		remaining -= n;
		if(!sendNext()) {
			dumpFile.close();
		}
	});

	req->submit();

	return true;
}

String ModbusHandler::getMethod() const
{
	return METHOD_MODBUS;
}
