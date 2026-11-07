#include "Listener.h"
#include "PacketLog.h"
#include <iostream>

Listener::Listener(Headphones& headphones, BluetoothWrapper& bt, std::atomic<bool>& running): 
_headphones(headphones),
_bt(bt),
_running(running)
{}

void Listener::listen()
{
    while (this->_running)
    {
        try
        {
            Buffer reply = this->_bt.readReplies();
            if (!this->_running) break;
            this->handle_message(reply);
        }
        catch (const std::exception& e)
        {
            if (!this->_running) break;
            std::cerr << "Listener error: " << e.what() << std::endl;
            PacketLog::note(e.what());
            // Once the connection is gone every read fails immediately; stop instead of spinning.
            if (!this->_bt.isConnected()) break;
        }
    }
}

inline BtMessage Listener::parse(Buffer msg)
{
    return CommandSerializer::unpackBtMessage(msg);
}

void Listener::handle_message(Buffer msg)
{
    BtMessage m = this->parse(msg);
    bool handled = m.dataType == DATA_TYPE::ACK || m.dataType == DATA_TYPE::DATA_MDR || m.dataType == DATA_TYPE::DATA_MDR_NO2;
    PacketLog::log(false, m.dataType, m.seqNumber, m.messageBytes, handled ? nullptr : "not handled, no ACK sent");

    if (m.dataType == DATA_TYPE::ACK)
    {
        this->_bt.setSeqNumber(m.seqNumber);
        this->_bt.postAck();
        this->_bt.notifyAck();
    } 
    else if (m.dataType == DATA_TYPE::DATA_MDR || m.dataType == DATA_TYPE::DATA_MDR_NO2) 
    {
        this->_headphones.setStateFromReply(m);
        this->_bt.sendAck(m.seqNumber);
    }
}
