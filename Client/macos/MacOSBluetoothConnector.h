#pragma once
#include <stdio.h>
#include "../IBluetoothConnector.h"
#include "IOBluetooth/IOBluetooth.h"
#include "Constants.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <future>

class MacOSBluetoothConnector final : public IBluetoothConnector
{
public:
    MacOSBluetoothConnector();
    ~MacOSBluetoothConnector();
    static void connectToMac(MacOSBluetoothConnector* MacOSBluetoothConnector, std::promise<void> connectPromise) noexcept(false);
    virtual void connect(const std::string& addrStr) noexcept(false);
    virtual int send(char* buf, size_t length) noexcept(false);
    virtual int recv(char* buf, size_t length) noexcept(false);
    virtual void disconnect() noexcept;
    virtual bool isConnected() noexcept;
    virtual void closeConnection();
    
    virtual std::vector<BluetoothDevice> getConnectedDevices() noexcept(false);
    // Called from the RFCOMM delegate when the remote side closes the channel.
    void onChannelClosed() noexcept;
    std::deque<std::vector<unsigned char>> receivedBytes;
    std::mutex receiveDataMutex;
    std::condition_variable receiveDataConditionVariable;
    std::atomic<bool> running = false;
    std::mutex disconnectionMutex;
    std::condition_variable disconnectionConditionVariable;
    
private:
    void stopRunning() noexcept;

    // Objective-C objects held with a +1 retain (__bridge_retained) so they stay
    // valid after IOBluetooth drops its own references, e.g. when the remote side
    // closes the channel. Released exactly once in closeConnection()/the destructor.
    // channelMutex guards rfcommchannel and commDelegate, which are used from the
    // I/O queue, the listener thread and the main thread.
    void *rfcommDevice = nullptr;
    void *rfcommchannel = nullptr;
    void *commDelegate = nullptr;
    std::mutex channelMutex;
    std::thread uthread;
};
