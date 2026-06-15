#include "Listener.h"
#include <iostream>

Listener::Listener(Headphones& headphones, BluetoothWrapper& bt): 
_headphones(headphones),
_bt(bt)
{}

void Listener::listen()
{
    for(;;)
    {
        try
        {
            Buffer reply = this->_bt.readReplies();
            this->handle_message(reply);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Listener error: " << e.what() << std::endl;
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

    if (m.dataType == DATA_TYPE::ACK)
    {
        this->_bt.setSeqNumber(m.seqNumber);
        this->_bt.postAck();
        this->_bt._ack.notify_all();
    } 
    else if (m.dataType == DATA_TYPE::DATA_MDR || m.dataType == DATA_TYPE::DATA_MDR_NO2) 
    {
        // Set these values as current values of Headphone property
        this->_headphones.setStateFromReply(m);
        this->_bt.sendAck(m.seqNumber);
    }
}
