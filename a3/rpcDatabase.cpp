#include "rpcDatabase.h"
#include "common.h"

//------------------------------------------------------
// Proc
//------------------------------------------------------
Proc::Proc(string& name, int* argTypes)
  :myName(name), myArgTypes(argTypes) {}

bool Proc::isSameSignature(string name, int* argTypes) {
  if (name.compare(myName) != 0) {
    return false;
  }
  int len = argTypesLength(argTypes);
  if (len != argTypesLength(myArgTypes)) {
    return false;
  }

  for (int i = 0; i < len; i++) {
    // check for same type
    if (((argTypes[i] & (15 << 16)) >> 16) != ((myArgTypes[i] & (15 << 16)) >> 16)) {
      return false;
    }

    // check for scalar vs array
    if ((((argTypes[i] & ((1 << 16) - 1)) == 0) && ((myArgTypes[i] & ((1 << 16) - 1)) != 0))
        || (((argTypes[i] & ((1 << 16) - 1)) != 0) && ((myArgTypes[i] & ((1 << 16) - 1)) == 0))) {
      return false;
    }
  }
  return true;
}

//------------------------------------------------------
// ServerLocation
//------------------------------------------------------
ServerLocation::ServerLocation(string& serverId, int port)
  :myServerId(serverId), myPort(port) {}

bool ServerLocation::isMatchingLocation(string server, int port) {
  if (server.compare(myServerId) == 0 && port == myPort) {
    return true;
  }
  return false;
}

//------------------------------------------------------
// ServerProcList
//------------------------------------------------------
ServerProcList::ServerProcList(string& serverId, int port, int socketFd)
  :mySocketFd(socketFd), myLocation(serverId, port) {}

void ServerProcList::add(string& name, int* argTypes) {
  int loc = -1;;

  // search for match
  for (unsigned int i = 0; i < myProcs.size(); i++) {
    if (myProcs[i].isSameSignature(name, argTypes)) {
      loc = i;
      break;
    }
  }

  // delete the match
  if (loc >= 0) {
    myProcs.erase(myProcs.begin() + loc);
  }

  Proc proc(name, argTypes);
  myProcs.push_back(proc);
}

//------------------------------------------------------
// RpcDatabase
//------------------------------------------------------
int RpcDatabase::add(string server, int port, int socketFd, string functionName, int* argTypes) {
  // search for a server
  for (unsigned int i = 0; i < myServers.size(); i++) {
    if (myServers[i].myLocation.isMatchingLocation(server, port)) {
      for (unsigned int j = 0; j < myServers[i].myProcs.size(); j++) {
        if (myServers[i].myProcs[j].isSameSignature(functionName, argTypes)) {
          myServers[i].add(functionName, argTypes);
          return SIGNATURE_ALREADY_EXISTS;
        }
      }
      myServers[i].add(functionName, argTypes);
      return REGISTER_SUCCESS;
    }
  }

  // not found, add the server to the list
  ServerProcList procList(server, port, socketFd);
  procList.add(functionName, argTypes);
  myServers.push_back(procList);
  return REGISTER_SUCCESS;
}

void RpcDatabase::remove(int socketFd) {
  int loc = -1;
  for (unsigned int i = 0; i < myServers.size(); i++) {
    struct ServerProcList server = myServers[i];
    if (server.mySocketFd == socketFd) {
      loc = i;
      break;
    }
  }

  // delete the match
  if (loc >= 0) {
    myServers.erase(myServers.begin() + loc);
  }
}



ServerLocation RpcDatabase::getProcLocation(string& name, int* argTypes) {
  string str = "";
  ServerLocation ret(str, -1);
  bool found = false;
  int loc = -1;;

  // search for a server
  for (unsigned int i = 0; i < myServers.size() && !found; i++) {
    for (unsigned int j = 0; j < myServers[i].myProcs.size() && !found; j++) {
      if (myServers[i].myProcs[j].isSameSignature(name, argTypes)) {
        found = true;
        ret = myServers[i].myLocation;
        loc = i;
        break;
      }
    }
  }

  // rearrange the list
  if (loc >= 0) {
    ServerProcList procList = myServers[loc];
    myServers.erase(myServers.begin() + loc);
    myServers.push_back(procList);
  }

  return ret;
}


vector<ServerProcList> RpcDatabase::getServers() {
  return myServers;
}

bool RpcDatabase::isEmpty() {
  return myServers.empty();
}
