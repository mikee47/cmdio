#pragma once

#include <cmdio/CommandHandler.h>
#include <IO/Modbus/Device.h>

class ModbusHandler : public WSCommandHandler
{
public:
	String getMethod() const override;

	ACL getAccess() const override
	{
		return {UserRole::Admin, UserRole::Admin};
	}

	PageInfo getPageInfo() const override
	{
		return {getAccess(), F("Modbus")};
	}

	void handleMessage(WSCommandConnection* connection, JsonObject json) override;

private:
	IO::Modbus::Device* device{nullptr};
	IO::Modbus::Function function{};
	uint16_t address{0};
	unsigned remaining{0};
};
