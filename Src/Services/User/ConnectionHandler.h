
#pragma once

#include <nstd/HashMap.h>

#include "Tools/ZlimdbConnection.h"
#include "Tools/ZlimdbProtocol.h"

class User2;
class Market2;
class Session2;

class ConnectionHandler : public ZlimdbConnection::Callback
{
public:
  ConnectionHandler() {}
  ~ConnectionHandler();

  const String& getErrorString() const {return error;}

  void_t addBotMarket(const String& name, const String& executable);
  void_t addBotEngine(const String& name, const String& executable);

  bool_t connect();
  void_t disconnect();
  bool_t process();

private:
  struct BotMarket
  {
    String name;
    String executable;
  };

  struct BotEngine
  {
    String name;
    String executable;
  };

  enum ProcessType
  {
    userMarket,
    userSession,
  };

  struct Process
  {
    ProcessType type;
    uint64_t entityId;
    uint32_t tableId;
  };

  struct Table
  {
    ProcessType type;
    void_t* object;
  };

private:
  ZlimdbConnection connection;
  String error;
  HashMap<String, BotMarket> botMarketsByName;
  HashMap<String, BotEngine> botEnginesByName;
  HashMap<uint64_t, BotMarket*> botMarkets;
  HashMap<uint64_t, BotEngine*> botEngines;
  HashMap<String, User2*> users;
  uint32_t processesTableId;
  HashMap<uint64_t, Process> processes;
  HashMap<uint32_t, Process*> processesByTable;
  HashMap<uint32_t, Table> tables;

private:
  User2* findUser(const String& name) {return *users.find(name);}
  User2* createUser(const String& name);

  bool_t setUserMarketState(Market2& market, meguco_user_market_state state);
  bool_t setUserSessionState(Session2& session, meguco_user_session_state state);

  void_t addedTable(uint32_t tableId, const String& tableName);
  void_t addedProcess(uint64_t entityId, const String& command);
  void_t addedUserMarket(uint32_t tableId, const String& userName, const meguco_user_market_entity& userMarket);
  void_t addedUserSession(uint32_t tableId, const String& userName, const meguco_user_session_entity& userSession);
  void_t updatedUserMarket(Market2& market, const meguco_user_market_entity& entity);
  void_t updatedUserSession(Session2& session, const meguco_user_session_entity& entity);
  void_t removedTable(uint32_t tableId);
  void_t removedProcess(uint64_t entityId);
  void_t removedUserMarket(Market2& market);
  void_t removedUserSession(Session2& session);

private: // ZlimdbConnection::Callback
  virtual void_t addedEntity(uint32_t tableId, const zlimdb_entity& entity);
  virtual void_t updatedEntity(uint32_t tableId, const zlimdb_entity& entity);
  virtual void_t removedEntity(uint32_t tableId, uint64_t entityId);
  virtual void_t controlEntity(uint32_t tableId, uint64_t entityId, uint32_t controlCode, const Buffer& buffer) {}
};
