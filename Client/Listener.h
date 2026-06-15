#pragma once

#include "Constants.h"
#include "BluetoothWrapper.h"
#include "CommandSerializer.h"
#include "Exceptions.h"
#include "Headphones.h"

#include <mutex>
#include <future>
#include <atomic>

class Listener
{
public:
    Listener(Headphones& headphones, BluetoothWrapper& bt, std::atomic<bool>& running);
    void listen();
    inline BtMessage parse(Buffer msg);
    void handle_message(Buffer msg);

private:
    Headphones& _headphones;
    BluetoothWrapper& _bt;
    std::atomic<bool>& _running;
};