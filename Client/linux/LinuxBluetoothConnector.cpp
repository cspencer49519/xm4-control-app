#include "LinuxBluetoothConnector.h"

#include <cstddef>
#include <stdio.h>
#include <unistd.h>

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>

#include "DBusHelper.h"

constexpr int CONNECT_TIMEOUT_SEC = 10;

LinuxBluetoothConnector::LinuxBluetoothConnector()
{
}
LinuxBluetoothConnector::~LinuxBluetoothConnector()
{
  //onclose event
  if (isConnected())
  {
    disconnect();
  }
}

int LinuxBluetoothConnector::recv(char *buf, size_t length)
{
  size_t read = ::read(this->_socket, buf, length);
  return read;
}

int LinuxBluetoothConnector::send(char *buf, size_t length)
{
  size_t written = ::write(this->_socket, buf, length);
  return written;
}

void LinuxBluetoothConnector::connect(const std::string &addrStr)
{
  struct sockaddr_rc addr = {0};
  const char *dest = addrStr.c_str();

  int sock = ::socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
  if (sock < 0)
    throw RecoverableException("Error: could not create bluetooth socket", true);

  this->_socket = sock;

  struct timeval timeout;
  timeout.tv_sec = CONNECT_TIMEOUT_SEC;
  timeout.tv_usec = 0;
  setsockopt(this->_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  setsockopt(this->_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  uint32_t linkmode = RFCOMM_LM_AUTH | RFCOMM_LM_ENCRYPT;
  setsockopt(this->_socket, SOL_RFCOMM, RFCOMM_LM, &linkmode, sizeof(linkmode));

  addr.rc_family = AF_BLUETOOTH;
  uint8_t channel = sdp_getServiceChannel(addrStr.c_str(), SERVICE_UUID_IN_BYTES);
  addr.rc_channel = channel;
  str2ba(dest, &addr.rc_bdaddr);

  int status = ::connect(this->_socket, (struct sockaddr *)&addr, sizeof(addr));

  if (status < 0)
  {
    this->_socket = -1;
    ::close(sock);
    throw RecoverableException("Error: could not connect to bluetooth socket", true);
  }
  this->_connected = true;
}

std::vector<BluetoothDevice> LinuxBluetoothConnector::getConnectedDevices()
{

  std::vector<BluetoothDevice> res;

  DBusConnection *connection = dbus_open_system_bus();
  if (NULL == connection)
  {
    return res;
  }

  std::vector<std::string> adapter_paths = dbus_list_adapters(connection);
  for (auto &adapter : adapter_paths)
  {
    std::string name = dbus_get_property(connection, adapter.c_str(), "Name");
    std::string address = dbus_get_property(connection, adapter.c_str(), "Address");
    res.push_back({.name = name, .mac = address});
  }

  return res;
}

void LinuxBluetoothConnector::disconnect() noexcept
{
  // close connection
  if (this->_socket != -1)
  {
    ::close(this->_socket);
  }
  this->_connected = false;
}

bool LinuxBluetoothConnector::isConnected() noexcept
{
  return this->_connected;
}
