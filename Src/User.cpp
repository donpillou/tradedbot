
#include "User.h"
#include "Session.h"
#include "ClientHandler.h"

User::User(ServerHandler& serverHandler) : serverHandler(serverHandler), nextSessionId(1) {}

User::~User()
{
  for(HashMap<uint32_t, Session*>::Iterator i = sessions.begin(), end = sessions.end(); i != end; ++i)
    delete *i;
}

void_t User::registerClient(ClientHandler& client)
{
  clients.append(&client);
}

void_t User::unregisterClient(ClientHandler& client)
{
  clients.remove(&client);
}

uint32_t User::createSession(const String& name, const String& engine, double balanceBase, double balanceComm)
{
  uint32_t id = nextSessionId++;
  Session* session = new Session(serverHandler, id, name, true);
  if(!session->start(engine, balanceBase, balanceComm))
  {
    delete session;
    return 0;
  }
  sessions.append(id, session);
  return id;
}

bool_t User::deleteSession(uint32_t id)
{
  HashMap<uint32_t, Session*>::Iterator it = sessions.find(id);
  if(it == sessions.end())
    return false;
  delete *it;
  sessions.remove(it);
  return true;
}

void_t User::sendClients(const byte_t* data, size_t size)
{
  for(HashSet<ClientHandler*>::Iterator i = clients.begin(), end = clients.end(); i != end; ++i)
  {
    ClientHandler* clientHandler = *i;
    clientHandler->send(data, size);
  }
}