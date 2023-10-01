#pragma once

#include <cmdio/CommandHandler.h>
#include <IO/Modbus/Device.h>

class ModbusHandler : public WSCommandHandler
{
public:
	String getMethod() const override;

	UserRole getMinAccess() const override
	{
		return UserRole::Admin;
	}

	PageInfo getPageInfo() const override
	{
		return {getMinAccess(), F("Modbus")};
	}

	void handleMessage(WSCommandConnection* connection, JsonObject json) override;

private:
	IO::Modbus::Device* device{nullptr};
	IO::Modbus::Function function{};
	uint16_t address{0};
	unsigned remaining{0};
};
