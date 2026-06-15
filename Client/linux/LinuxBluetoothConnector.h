#pragma once
#include <stdio.h>
#include "../IBluetoothConnector.h"
#include "Constants.h"
#include <atomic>

class LinuxBluetoothConnector final : public IBluetoothConnector
{
public:
	LinuxBluetoothConnector();
	~LinuxBluetoothConnector();
	virtual void connect(const std::string& addrStr) noexcept(false);
	virtual int send(char* buf, size_t length) noexcept(false);
	virtual int recv(char* buf, size_t length) noexcept(false);
	virtual void disconnect() noexcept;
	virtual bool isConnected() noexcept;
	virtual std::vector<BluetoothDevice> getConnectedDevices() noexcept(false);
	virtual int getBatteryLevel() const;


private:
	int _socket = -1;
	std::atomic<bool> _connected = false;
	mutable int _batteryLevel = -1;
	mutable bool _batteryQueried = false;
	std::string _mac;
};