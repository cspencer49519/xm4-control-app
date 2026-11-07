#include "MacOSBluetoothConnector.h"

MacOSBluetoothConnector::MacOSBluetoothConnector()
{
    
}
MacOSBluetoothConnector::~MacOSBluetoothConnector()
{
    disconnect();
    if (rfcommDevice) {
        CFBridgingRelease(rfcommDevice);
        rfcommDevice = nullptr;
    }
}

@interface AsyncCommDelegate : NSObject <IOBluetoothRFCOMMChannelDelegate> {
@public
    MacOSBluetoothConnector* delegateCPP;
}
@end

@implementation AsyncCommDelegate {
}
- (void)rfcommChannelClosed:(IOBluetoothRFCOMMChannel *)rfcommChannel{
    // Runs on the connection thread, so it must not join that thread (disconnect() does).
    if (delegateCPP)
        delegateCPP->onChannelClosed();
}

-(void)rfcommChannelData:(IOBluetoothRFCOMMChannel *)rfcommChannel data:(void *)dataPointer length:(size_t)dataLength
{
    if (!delegateCPP)
        return;
    std::lock_guard<std::mutex> g(delegateCPP->receiveDataMutex);
    
    unsigned char* buffer = (unsigned char*)dataPointer;
    std::vector<unsigned char> vectorBuffer(buffer, buffer+dataLength);
    
    delegateCPP->receivedBytes.push_back(vectorBuffer);
    delegateCPP->receiveDataConditionVariable.notify_one();
}


@end

int MacOSBluetoothConnector::send(char* buf, size_t length)
{
    std::lock_guard<std::mutex> g(channelMutex);
    IOBluetoothRFCOMMChannel *chan = (__bridge IOBluetoothRFCOMMChannel*)rfcommchannel;
    if (chan == nil || [chan writeSync:(void*)buf length:(UInt16)length] != kIOReturnSuccess)
        throw RecoverableException("Couldn't send to the headphones.", true);
    return (int)length;
}


void MacOSBluetoothConnector::connectToMac(MacOSBluetoothConnector* macOSBluetoothConnector, std::promise<void> connectPromise)
{
    // get device
    IOBluetoothDevice *device = (__bridge IOBluetoothDevice *)macOSBluetoothConnector->rfcommDevice;
    // create new channel
    IOBluetoothRFCOMMChannel *channel = [[IOBluetoothRFCOMMChannel alloc] init];
    // create sppServiceid
    IOBluetoothSDPUUID *sppServiceUUID = [IOBluetoothSDPUUID uuidWithBytes:(void*)SERVICE_UUID_IN_BYTES length: 16];
    // get sppServiceRecord
    IOBluetoothSDPServiceRecord *sppServiceRecord = [device getServiceRecordForUUID:sppServiceUUID];
    // get rfcommChannelID from sppServiceRecord
    UInt8 rfcommChannelID = 0;
    if (sppServiceRecord == nil || [sppServiceRecord getRFCOMMChannelID:&rfcommChannelID] != kIOReturnSuccess) {
        connectPromise.set_exception(std::make_exception_ptr(
            RecoverableException("This device doesn't expose the Sony headphones service.", false)));
        return;
    }
    // setup delegate
    AsyncCommDelegate* asyncCommDelegate = [[AsyncCommDelegate alloc] init];
    asyncCommDelegate->delegateCPP = macOSBluetoothConnector;
    // try to open channel
    if ( [device openRFCOMMChannelAsync:&channel withChannelID:rfcommChannelID delegate:asyncCommDelegate] != kIOReturnSuccess ) {
        asyncCommDelegate->delegateCPP = nullptr;
        RecoverableException exc = RecoverableException("Could not open the rfcomm.", false);
        std::exception_ptr excPtr = std::make_exception_ptr(exc);
        connectPromise.set_exception(excPtr);
        return;
    }
    // keep the channel and its delegate alive until closeConnection()
    {
        std::lock_guard<std::mutex> g(macOSBluetoothConnector->channelMutex);
        macOSBluetoothConnector->rfcommchannel = (__bridge_retained void*) channel;
        macOSBluetoothConnector->commDelegate = (__bridge_retained void*) asyncCommDelegate;
    }
    
    macOSBluetoothConnector->running = true;
    
    // tell the other tread that we are done connecting
    connectPromise.set_value();

    // keep thread running, until we are disconnected
    std::unique_lock<std::mutex> lk(macOSBluetoothConnector->disconnectionMutex);
    while (macOSBluetoothConnector->running) {
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.1]];

        macOSBluetoothConnector->disconnectionConditionVariable.wait_for(
            lk, std::chrono::milliseconds(1000),
            [&]() { return !macOSBluetoothConnector->running; });
    }

    lk.unlock();
}
void MacOSBluetoothConnector::connect(const std::string& addrStr){
    // convert mac address to nsstring
    NSString *addressNSString = [NSString stringWithCString:addrStr.c_str() encoding:[NSString defaultCStringEncoding]];
    // get device based on mac address
    IOBluetoothDevice *device = [IOBluetoothDevice deviceWithAddressString:addressNSString];
    if (device == nil) {
        throw RecoverableException("Invalid Bluetooth address.", false);
    }
    // if device is not connected
    if (![device isConnected]) {
        [device openConnection];
    }
    // clean up after a previous connection whose thread already finished
    if (uthread.joinable()) {
        running = false;
        disconnectionConditionVariable.notify_all();
        uthread.join();
    }
    {
        std::lock_guard<std::mutex> g(receiveDataMutex);
        receivedBytes.clear();
    }
    std::promise<void> connectPromise;
    std::future<void> connectFuture = connectPromise.get_future();

    // keep the device alive for the connection thread
    if (rfcommDevice)
        CFBridgingRelease(rfcommDevice);
    rfcommDevice = (__bridge_retained void*) device;
    uthread = std::thread(MacOSBluetoothConnector::connectToMac, this, std::move(connectPromise));
    
    // wait till the device is connected
    try {
        connectFuture.get();
    } catch (...) {
        // the connection thread has already returned; reap it so a later connect can reuse uthread
        if (uthread.joinable())
            uthread.join();
        throw;
    }
}

int MacOSBluetoothConnector::recv(char* buf, size_t length)
{
    // wait for newly received data
    std::unique_lock<std::mutex> g(receiveDataMutex);
    receiveDataConditionVariable.wait(g, [this]{ return !receivedBytes.empty() || !running; });
    if (receivedBytes.empty())
        return 0; // disconnected
    
    // fill the buf with the new data
    std::vector<unsigned char> receivedVector = receivedBytes.front();
    receivedBytes.pop_front();
    
    size_t lengthCopied = std::min(length, receivedVector.size());

    // copy the first amount of bytes
    std::memcpy(buf, receivedVector.data(), lengthCopied);
    
    // too much data, save it for next time
    if (receivedVector.size() > length){
        receivedVector.erase(receivedVector.begin(), receivedVector.begin() + lengthCopied);
        receivedBytes.push_front(receivedVector);
    }
    
    return (int)lengthCopied;
}

std::vector<BluetoothDevice> MacOSBluetoothConnector::getConnectedDevices()
{
    // create the output vector
    std::vector<BluetoothDevice> res;
    // loop through the paired devices (also includes non paired devices for some reason)
    for (IOBluetoothDevice* device in [IOBluetoothDevice pairedDevices]) {
        // check if device is connected
        if ([device isConnected]) {
            BluetoothDevice dev;
            // save the mac address and name
            dev.mac = [[device addressString] UTF8String];
            dev.name = [[device nameOrAddress] UTF8String] ?: "";
            // add device to the connected devices vector
            res.push_back(dev);
        }
    }
    
    return res;
}

void MacOSBluetoothConnector::stopRunning() noexcept
{
    running = false;
    // wake the connection thread
    disconnectionConditionVariable.notify_all();
    // wake any reader blocked in recv()
    {
        std::lock_guard<std::mutex> g(receiveDataMutex);
    }
    receiveDataConditionVariable.notify_all();
}

void MacOSBluetoothConnector::onChannelClosed() noexcept
{
    stopRunning();
}

void MacOSBluetoothConnector::disconnect() noexcept
{
    // close connection
    closeConnection();
    stopRunning();
    // wait for the thread to finish (it may already have, or never have started)
    if (uthread.joinable() && uthread.get_id() != std::this_thread::get_id())
        uthread.join();
}
void MacOSBluetoothConnector::closeConnection() {
    // get the channel
    IOBluetoothRFCOMMChannel *chan = nil;
    AsyncCommDelegate *delegate = nil;
    {
        std::lock_guard<std::mutex> g(channelMutex);
        // take ownership back; ARC releases both at the end of this function
        chan = (__bridge_transfer IOBluetoothRFCOMMChannel*) rfcommchannel;
        delegate = (__bridge_transfer AsyncCommDelegate*) commDelegate;
        rfcommchannel = nullptr;
        commDelegate = nullptr;
    }
    if (delegate)
        delegate->delegateCPP = nullptr;
    if (chan == nil)
        return;
    [chan setDelegate: nil];
    // close the channel (harmless if the remote side already closed it)
    [chan closeChannel];
}


bool MacOSBluetoothConnector::isConnected() noexcept
{
    if (!running)
        return false;
    std::lock_guard<std::mutex> g(channelMutex);
    IOBluetoothRFCOMMChannel *chan = (__bridge IOBluetoothRFCOMMChannel*) rfcommchannel;
    return chan.isOpen;
}
