#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <algorithm>
#include <vector>
#include <string>
#include "rpcDatabase.h"
#include "binder.h"
#include "common.h"

using namespace std;

Binder::Binder() {
  shutdown = false;
  rpcDatabase = new RpcDatabase();
}

Binder::~Binder() {
  delete rpcDatabase;
}

void Binder::start() {
  int status;
  struct addrinfo hints;
  struct addrinfo* servinfo;
  struct addrinfo* p;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  status = getaddrinfo(NULL, "0", &hints, &servinfo);
  if (status != 0) {
    fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
    return;
  }

  p = servinfo;
  int sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

  status = bind(sock, servinfo->ai_addr, servinfo->ai_addrlen);

  status = listen(sock, 5);

  // Get the hostname and port and print them out
  char hostname[256];
  gethostname(hostname, 256);
  cout << "BINDER_ADDRESS " << hostname << endl;

  struct sockaddr_in sin;
  socklen_t len = sizeof(sin);
  getsockname(sock, (struct sockaddr *)&sin, &len);
  cout << "BINDER_PORT " << ntohs(sin.sin_port) << endl;

  fd_set readfds;
  int n;
  struct sockaddr_storage their_addr;

  while (true) {

    // build the connection list
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    n = sock;
    for (vector<int>::iterator it = myConnections.begin();
        it != myConnections.end(); ++it) {
      int connection = *it;
      FD_SET(connection, &readfds);
      if (connection > n)
        n = connection;
    }
    n = n+1;

    status = select(n, &readfds, NULL, NULL, NULL);

    if (status == -1) {
      cerr << "ERROR: select failed." << endl;
    } else {
      // one or both of the descriptors have data
      if (FD_ISSET(sock, &readfds)) {
        // ready to accept
        socklen_t addr_size = sizeof their_addr;
        int new_sock = accept(sock, (struct sockaddr*)&their_addr, &addr_size);

        if (new_sock < 0) {
          cerr << "ERROR: while accepting connection" << endl;
          close(new_sock);
          continue;
        }

        // add new connection
        add_connection(new_sock);

      } else {
        // a connection is ready to send us stuff
        for (vector<int>::iterator it = myConnections.begin();
            it != myConnections.end(); ++it) {
          int connection = *it;
          if (FD_ISSET(connection, &readfds)) {
            process_connection(connection);
          }
        }
      }
    }
    close_connections();
    // shutdown the binder once all servers have disconnected
    if (shutdown && rpcDatabase->isEmpty()) {
      break;
    }
  }

  // free the linked list
  freeaddrinfo(servinfo);
}

void Binder::add_connection(int sock) {
  myConnections.push_back(sock);
}

void Binder::close_connections() {
  for (vector<int>::iterator it = myToRemove.begin();
      it != myToRemove.end(); ++it) {
    myConnections.erase(remove(myConnections.begin(), myConnections.end(), *it), myConnections.end());
    close(*it);
  }
  myToRemove.clear();
}

void Binder::process_connection(int sock) {
  int status;

  // receive the buffer length
  int msg_type = 0;
  status = recv(sock, &msg_type, sizeof msg_type, 0);
  if (status < 0) {
    cerr << "ERROR: receive failed" << endl;
    return;
  }

  if (status == 0) {
    //remote end has closed the connection
    //deregister the server if the connection was a server
    rpcDatabase->remove(sock);
    myToRemove.push_back(sock);
    return;
  }

  // if in shutdown phase, don't handle any messages except server shutdown
  if (shutdown) {
    return;
  }

  switch (msg_type) {
    case MSG_TERMINATE: {
      // check that the sender has the right address
      struct CLIENT_BINDER_TERMINATE* res = CLIENT_BINDER_TERMINATE::readMessage(sock);
      char binderHostname[256];
      gethostname(binderHostname, 256);
      if (strcmp(res->hostname, binderHostname) != 0)
        return;

      // tell servers to shutdown
      terminateServers();
      shutdown = true;
      break;
    }
    case MSG_REGISTER: {
      struct SERVER_BINDER_REGISTER* res = SERVER_BINDER_REGISTER::readMessage(sock);
      int reg = rpcDatabase->add(res->server_identifier, res->port, sock, res->name, res->argTypes);
      // successful registration
      if (reg >= 0) {
        struct SERVER_BINDER_REGISTER_SUCCESS msg;
        msg.warningCode = reg; // come up with warnings if necessary
        status = msg.sendMessage(sock);
      } else {
        // failed registration
        struct SERVER_BINDER_REGISTER_FAILURE msg;
        msg.failureCode = REGISTER_FAILURE;
        status = msg.sendMessage(sock);
      }
      break;
    }
    case MSG_LOC_REQUEST: {
      struct CLIENT_BINDER_LOC_REQUEST* res = CLIENT_BINDER_LOC_REQUEST::readMessage(sock);
      string serverName = res->name;
      ServerLocation loc = rpcDatabase->getProcLocation(serverName, res->argTypes);

      // valid server found that will handle requests
      if (loc.myPort != -1) {
        struct CLIENT_BINDER_LOC_SUCCESS msg;
        char *l = new char[STR_LEN];
        strcpy(l, loc.myServerId.c_str());
        msg.server_identifier = l;
        msg.port = loc.myPort;
        status = msg.sendMessage(sock);
      } else {
        // no server matching the function signature found
        struct CLIENT_BINDER_LOC_FAILURE msg;
        msg.reasonCode = NO_MATCHING_SIGNATURE;
        status = msg.sendMessage(sock);
      }
      break;
    }
  }
}

void Binder::terminateServers() {
  vector<ServerProcList> servers = rpcDatabase->getServers();
  for (vector<ServerProcList>::iterator it = servers.begin(); it != servers.end(); ++it) {
    int status;
    ServerProcList server = *it;
    int msg_type = MSG_TERMINATE;
    status = send(server.mySocketFd, &msg_type, sizeof(msg_type), 0);
  }
}


int main() {
  Binder binder;
  binder.start();
  return 0;
}
