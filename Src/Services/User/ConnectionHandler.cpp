
#include <nstd/Error.h>

#include "Tools/ZlimdbProtocol.h"

#include "ConnectionHandler.h"
#include "User2.h"
#include "Market2.h"
#include "Session2.h"

ConnectionHandler::~ConnectionHandler()
{
  disconnect();
}

void_t ConnectionHandler::addBotMarket(const String& name, const String& executable)
{
  BotMarket botMarket = {name, executable};
  botMarketsByName.append(name, botMarket);
}

void_t ConnectionHandler::addBotEngine(const String& name, const String& executable)
{
  BotEngine botEngine = {name, executable};
  botEnginesByName.append(name, botEngine);
}

void_t ConnectionHandler::disconnect()
{
  connection.close();
  for(HashMap<String, User2*>::Iterator i = users.begin(), end = users.end(); i != end; ++i)
    delete *i;
  users.clear();
  processes.clear();
  processesByTable.clear();
  tables.clear();
}

bool_t ConnectionHandler::connect()
{
  disconnect();

  if(!connection.connect(*this))
    return error = connection.getErrorString(), false;

  Buffer buffer(ZLIMDB_MAX_MESSAGE_SIZE);

  // update bot markets table
  {
    HashMap<String, uint64_t> knownBotMarkets;
    uint32_t botMarketsTableId;
    if(!connection.createTable("botMarkets", botMarketsTableId))
      return error = connection.getErrorString(), false;
    if(!connection.query(botMarketsTableId))
      return error = connection.getErrorString(), false;
    String marketName;
    while(connection.getResponse(buffer))
    {
      void* data = (byte_t*)buffer;
      uint32_t size = buffer.size();
      for(const meguco_bot_market_entity* botMarket; botMarket = (const meguco_bot_market_entity*)zlimdb_get_entity(sizeof(meguco_bot_market_entity), &data, &size);)
      {
        if(!ZlimdbProtocol::getString(botMarket->entity, sizeof(*botMarket), botMarket->name_size, marketName))
          continue;
        knownBotMarkets.append(marketName, botMarket->entity.id);
      }
    }
    if(connection.getErrno() != 0)
      return error = connection.getErrorString(), false;
    buffer.resize(ZLIMDB_MAX_MESSAGE_SIZE);
    for(HashMap<String, BotMarket>::Iterator i = botMarketsByName.begin(), end = botMarketsByName.end(); i != end; ++i)
    {
      const String& marketName = i.key();
      HashMap<String, uint64_t>::Iterator it = knownBotMarkets.find(marketName);
      if(it == knownBotMarkets.end())
      {
        meguco_bot_market_entity* botMarket = (meguco_bot_market_entity*)(byte_t*)buffer;
        ZlimdbProtocol::setEntityHeader(botMarket->entity, 0, 0, sizeof(*botMarket) + marketName.length());
        ZlimdbProtocol::setString(botMarket->entity, botMarket->name_size, sizeof(*botMarket), marketName);
        uint64_t id;
        if(!connection.add(botMarketsTableId, botMarket->entity, id))
          return error = connection.getErrorString(), false;
        botMarkets.append(id, &*i);
      }
      else
      {
        knownBotMarkets.remove(it);
        botMarkets.append(*it, &*i);
      }
    }
    for(HashMap<String, uint64_t>::Iterator i = knownBotMarkets.begin(), end = knownBotMarkets.end(); i != end; ++i)
      connection.remove(botMarketsTableId, *i);
  }

  // update bot engines list
  {
    HashMap<String, uint64_t> knownBotEngines;
    uint32_t botEnginesTableId;
    if(!connection.createTable("botEngines", botEnginesTableId))
      return error = connection.getErrorString(), false;
    if(!connection.query(botEnginesTableId))
      return error = connection.getErrorString(), false;
    String engineName;
    while(connection.getResponse(buffer))
    {
      void* data = (byte_t*)buffer;
      uint32_t size = buffer.size();
      for(const meguco_bot_engine_entity* botEngine; botEngine = (const meguco_bot_engine_entity*)zlimdb_get_entity(sizeof(meguco_bot_engine_entity), &data, &size);)
      {
        if(!ZlimdbProtocol::getString(botEngine->entity, sizeof(*botEngine), botEngine->name_size, engineName))
          continue;
        knownBotEngines.append(engineName, botEngine->entity.id);
      }
    }
    if(connection.getErrno() != 0)
      return error = connection.getErrorString(), false;
    buffer.resize(ZLIMDB_MAX_MESSAGE_SIZE);
    for(HashMap<String, BotEngine>::Iterator i = botEnginesByName.begin(), end = botEnginesByName.end(); i != end; ++i)
    {
      const String& botEngineName = i.key();
      HashMap<String, uint64_t>::Iterator it = knownBotEngines.find(botEngineName);
      if(it == knownBotEngines.end())
      {
        meguco_bot_engine_entity* botEngine = (meguco_bot_engine_entity*)(byte_t*)buffer;
        ZlimdbProtocol::setEntityHeader(botEngine->entity, 0, 0, sizeof(*botEngine) + botEngineName.length());
        ZlimdbProtocol::setString(botEngine->entity, botEngine->name_size, sizeof(*botEngine), botEngineName);
        uint64_t id;
        if(!connection.add(botEnginesTableId, botEngine->entity, id))
          return error = connection.getErrorString(), false;
        botEngines.append(id, &*i);
      }
      else
      {
        knownBotEngines.remove(it);
        botEngines.append(*it, &*i);
      }
    }
    for(HashMap<String, uint64_t>::Iterator i = knownBotEngines.begin(), end = knownBotEngines.end(); i != end; ++i)
      connection.remove(botEnginesTableId, *i);
  }

  // get processes
  if(!connection.createTable("processes", processesTableId))
      return error = connection.getErrorString(), false;
  if(connection.subscribe(processesTableId))
    return error = connection.getErrorString(), false;
  {
    String tableName;
    HashMap<uint64_t, String> processes;
    while(connection.getResponse(buffer))
    {
      void* data = (byte_t*)buffer;
      uint32_t size = buffer.size();
      String command;
      for(const meguco_process_entity* process; process = (const meguco_process_entity*)zlimdb_get_entity(sizeof(meguco_process_entity), &data, &size);)
      {
        if(!ZlimdbProtocol::getString(process->entity, sizeof(*process), process->cmd_size, command))
          continue;
        processes.append(process->entity.id, command);
      }
    }
    if(connection.getErrno() != 0)
      return error = connection.getErrorString(), false;
    for(HashMap<uint64_t, String>::Iterator i = processes.begin(), end = processes.end(); i != end; ++i)
      addedProcess(i.key(), *i);
  }

  // get table list
  if(connection.subscribe(zlimdb_table_tables))
    return error = connection.getErrorString(), false;
  {
    String tableName;
    HashMap<uint64_t, String> tables;
    while(connection.getResponse(buffer))
    {
      void* data = (byte_t*)buffer;
      uint32_t size = buffer.size();
      for(const zlimdb_table_entity* botMarket; botMarket = (const zlimdb_table_entity*)zlimdb_get_entity(sizeof(zlimdb_table_entity), &data, &size);)
      {
        if(!ZlimdbProtocol::getString(botMarket->entity, sizeof(*botMarket), botMarket->name_size, tableName))
          continue;
        tables.append((uint32_t)botMarket->entity.id, tableName);
      }
    }
    if(connection.getErrno() != 0)
      return error = connection.getErrorString(), false;
    for(HashMap<uint64_t, String>::Iterator i = tables.begin(), end = tables.end(); i != end; ++i)
      addedTable((uint32_t)i.key(), *i);
  }

  return true;
}

bool_t ConnectionHandler::process()
{
  for(;;)
    if(!connection.process())
      return false;
}

User2* ConnectionHandler::createUser(const String& name)
{
  User2* user = new User2();
  users.append(name, user);
  return user;
}

void_t ConnectionHandler::addedEntity(uint32_t tableId, const zlimdb_entity& entity)
{
  if(tableId == zlimdb_table_tables)
  {
    if(entity.size < sizeof(zlimdb_table_entity))
      return;
    const zlimdb_table_entity* tableEntity = (const zlimdb_table_entity*)&entity;
    String tableName;
    if(!ZlimdbProtocol::getString(tableEntity->entity, sizeof(*tableEntity), tableEntity->name_size, tableName))
      return;
    addedTable((uint32_t)tableEntity->entity.id, tableName);
  }
  else if(tableId == processesTableId)
  {
    if(entity.size < sizeof(meguco_process_entity))
      return;
    const meguco_process_entity* process = (const meguco_process_entity*)&entity;
    String command;
    if(!ZlimdbProtocol::getString(process->entity, sizeof(*process), process->cmd_size, command))
      return;
    addedProcess(process->entity.id, command);
  }
}

void_t ConnectionHandler::updatedEntity(uint32_t tableId, const zlimdb_entity& entity)
{
  if(entity.id != 1)
    return;
  HashMap<uint32_t, Table>::Iterator it = tables.find(tableId);
  if(it != tables.end())
  {
    Table& table = *it;
    switch(table.type)
    {
    case userMarket:
      if(entity.size >= sizeof(meguco_user_market_entity))
        updatedUserMarket(*(Market2*)table.object, *(const meguco_user_market_entity*)&entity);
      break;
    case userSession:
      if(entity.size >= sizeof(meguco_user_session_entity))
        updatedUserSession(*(Session2*)table.object, *(const meguco_user_session_entity*)&entity);
      break;
    }
  }
}

void_t ConnectionHandler::removedEntity(uint32_t tableId, uint64_t entityId)
{
  if(tableId == zlimdb_table_tables)
    removedTable((uint32_t)entityId);
  else if(tableId == processesTableId)
    removedProcess(entityId);
}

void_t ConnectionHandler::addedTable(uint32_t entityId, const String& tableName)
{
  if(tableName.startsWith("users/") && tableName.endsWith("/market"))
  {
    String userTable = tableName.substr(6, tableName.length() - (6 + 7));
    const char_t* userNameEnd = userTable.find('/');
    if(!userNameEnd)
      return;
    String userName = userTable.substr(0, userNameEnd - userTable);

    // subscribe to market
    if(!connection.subscribe(entityId))
      return;
    Buffer buffer;
    while(connection.getResponse(buffer))
    {
      void* data = (byte_t*)buffer;
      uint32_t size = buffer.size();
      for(const meguco_user_market_entity* userMarket; userMarket = (const meguco_user_market_entity*)zlimdb_get_entity(sizeof(meguco_user_market_entity), &data, &size);)
      {
        if(userMarket->entity.id != 1)
          continue;
        addedUserMarket(entityId, userName, *userMarket);
      }
    }
    if(connection.getErrno() != 0)
      return;
  }
  if(tableName.startsWith("users/") && tableName.endsWith("/session"))
  {
    String userTable = tableName.substr(6, tableName.length() - (6 + 8));
    const char_t* userNameEnd = userTable.find('/');
    if(!userNameEnd)
      return;
    String userName = userTable.substr(0, userNameEnd - userTable);

    // subscribe to session
    if(!connection.subscribe(entityId))
      return;
    Buffer buffer;
    while(connection.getResponse(buffer))
    {
      void* data = (byte_t*)buffer;
      uint32_t size = buffer.size();
      for(const meguco_user_session_entity* userSession; userSession = (const meguco_user_session_entity*)zlimdb_get_entity(sizeof(meguco_user_session_entity), &data, &size);)
      {
        if(userSession->entity.id != 1)
          continue;
        addedUserSession(entityId, userName, *userSession);
      }
    }
    if(connection.getErrno() != 0)
      return;
  }
}

void_t ConnectionHandler::addedProcess(uint64_t entityId, const String& command)
{
  if(command.startsWith("Markets/"))
  {
    const char_t* param = command.find(' ');
    if(param)
    {
      String paramStr = command.substr(param - command + 1);
      uint32_t tableId = paramStr.toUInt();
      Process processData = {userMarket, entityId, tableId};
      Process& process = processes.append(entityId, processData);
      processesByTable.append(tableId, &process);
      HashMap<uint32_t, Table>::Iterator it = tables.find(tableId);
      if(it != tables.end() && it->type == userMarket)
      {
        Market2* market = (Market2*)it->object;
        market->setState(meguco_user_market_running);
        if(!connection.update(tableId, market->getEntity()))
          return;
      }
    }
  }
  if(command.startsWith("Bots/"))
  {
    const char_t* param = command.find(' ');
    if(param)
    {
      String paramStr = command.substr(param - command + 1);
      uint32_t tableId = paramStr.toUInt();
      Process processData = {userSession, entityId, tableId};
      Process& process = processes.append(entityId, processData);
      processesByTable.append(tableId, &process);
      HashMap<uint32_t, Table>::Iterator it = tables.find(tableId);
      if(it != tables.end() && it->type == userSession)
      {
        Session2* session = (Session2*)it->object;
        session->setState(meguco_user_session_running);
        if(!connection.update(tableId, session->getEntity()))
          return;
      }
    }
  }
}

void_t ConnectionHandler::addedUserMarket(uint32_t tableId, const String& userName, const meguco_user_market_entity& userMarket)
{
  BotMarket* botMarket = *botMarkets.find(userMarket.bot_market_id);
  User2* user = findUser(userName);
  if(!user)
    user = createUser(userName);
  Market2* market = user->createMarket(tableId, userMarket, botMarket ? botMarket->executable : String());
  Table table = {ConnectionHandler::userMarket, market};
  tables.append(tableId, table);

  // start process
  if(!setUserMarketState(*market, meguco_user_market_starting))
    return;
}

bool_t ConnectionHandler::setUserMarketState(Market2& market, meguco_user_market_state state)
{
  uint32_t tableId = market.getTableId();
  HashMap<uint32_t, Process*>::Iterator it = processesByTable.find(tableId);
  if(it != processesByTable.end() && (*it)->type != userMarket)
    it = processesByTable.end();
  if(state == meguco_user_market_starting || state == meguco_user_market_running)
  {
    if(it == processesByTable.end())
    {
      if(market.getState() != meguco_user_market_starting)
      {
        market.setState(meguco_user_market_starting);
        if(!connection.update(tableId, market.getEntity()))
          return false;
      }
      String command = market.getExecutable() + " " + String::fromUInt(tableId);
      Buffer buffer;
      buffer.resize(sizeof(meguco_process_entity) + command.length());
      meguco_process_entity* process = (meguco_process_entity*)(byte_t*)buffer;
      ZlimdbProtocol::setEntityHeader(process->entity, 0, 0, buffer.size());
      ZlimdbProtocol::setString(process->entity, process->cmd_size, sizeof(*process), command);
      uint64_t id;
      if(!connection.add(processesTableId, process->entity, id))
        return false;
    }
    else if(market.getState() != meguco_user_market_running)
    {
      market.setState(meguco_user_market_running);
      if(!connection.update(tableId, market.getEntity()))
        return false;
    }
  }
  else
  {
    if(it != processesByTable.end())
    {
      if(market.getState() != meguco_user_market_stopping)
      {
        market.setState(meguco_user_market_stopping);
        if(!connection.update(tableId, market.getEntity()))
          return false;
      }
      if(!connection.remove(processesTableId, (*it)->entityId))
        return false;
    }
    else if(market.getState() != meguco_user_market_stopped)
    {
      market.setState(meguco_user_market_stopped);
      if(!connection.update(tableId, market.getEntity()))
        return false;
    }
  }
  return true;
}

bool_t ConnectionHandler::setUserSessionState(Session2& session, meguco_user_session_state state)
{
  uint32_t tableId = session.getTableId();
  HashMap<uint32_t, Process*>::Iterator it = processesByTable.find(tableId);
  if(it != processesByTable.end() && (*it)->type != userSession)
    it = processesByTable.end();
  if(state == meguco_user_session_starting || state == meguco_user_session_running)
  {
    if(it == processesByTable.end())
    {
      if(session.getState() != meguco_user_session_starting)
      {
        session.setState(meguco_user_session_starting);
        if(!connection.update(tableId, session.getEntity()))
          return false;
      }
      String command = session.getExecutable() + " " + String::fromUInt(tableId);
      Buffer buffer;
      buffer.resize(sizeof(meguco_process_entity) + command.length());
      meguco_process_entity* process = (meguco_process_entity*)(byte_t*)buffer;
      ZlimdbProtocol::setEntityHeader(process->entity, 0, 0, buffer.size());
      ZlimdbProtocol::setString(process->entity, process->cmd_size, sizeof(*process), command);
      uint64_t id;
      if(!connection.add(processesTableId, process->entity, id))
        return false;
    }
    else if(session.getState() != meguco_user_session_running)
    {
      session.setState(meguco_user_session_running);
      if(!connection.update(tableId, session.getEntity()))
        return false;
    }
  }
  else
  {
    if(it != processesByTable.end())
    {
      if(session.getState() != meguco_user_session_stopping)
      {
        session.setState(meguco_user_session_stopping);
        if(!connection.update(tableId, session.getEntity()))
          return false;
      }
      if(!connection.remove(processesTableId, (*it)->entityId))
        return false;
    }
    else if(session.getState() != meguco_user_session_stopped)
    {
      session.setState(meguco_user_session_stopped);
      if(!connection.update(tableId, session.getEntity()))
        return false;
    }
  }
  return true;
}

void_t ConnectionHandler::addedUserSession(uint32_t tableId, const String& userName, const meguco_user_session_entity& userSession)
{
  BotEngine* botEngine = *botEngines.find(userSession.bot_engine_id);
  User2* user = findUser(userName);
  if(!user)
    user = createUser(userName);
  Session2* session = user->createSession(tableId, userSession, botEngine ? botEngine->executable : String());
  Table table = {ConnectionHandler::userSession, session};
  tables.append(tableId, table);

  // start process
  if(!setUserSessionState(*session, (meguco_user_session_state)userSession.state))
    return;
}

void_t ConnectionHandler::updatedUserMarket(Market2& market, const meguco_user_market_entity& entity)
{
  if(!setUserMarketState(market, (meguco_user_market_state)entity.state))
    return;
}

void_t ConnectionHandler::updatedUserSession(Session2& session, const meguco_user_session_entity& entity)
{
  if(!setUserSessionState(session, (meguco_user_session_state)entity.state))
    return;
}

void_t ConnectionHandler::removedTable(uint32_t tableId)
{
  HashMap<uint32_t, Table>::Iterator it = tables.find(tableId);
  if(it != tables.end())
  {
    Table& table = *it;
    switch(table.type)
    {
    case userMarket:
      removedUserMarket(*(Market2*)table.object);
      break;
    case userSession:
      removedUserSession(*(Session2*)table.object);
      break;
    }
  }
}

void_t ConnectionHandler::removedProcess(uint64_t entityId)
{
  HashMap<uint64_t, Process>::Iterator it = processes.find(entityId);
  if(it != processes.end())
  {
    Process& process = *it;
    {
      HashMap<uint32_t, Table>::Iterator it = tables.find(process.tableId);
      if(it != tables.end())
      {
        Table& table = *it;
        switch(process.type)
        {
        case userMarket:
          {
            Market2* market = (Market2*)table.object;
            setUserMarketState(*market, meguco_user_market_stopped);
          }
          break;
        case userSession:
          {
            Session2* session = (Session2*)table.object;
            setUserSessionState(*session, meguco_user_session_stopped);
          }
          break;
        }
      }
    }
    processesByTable.remove(process.tableId);
    processes.remove(it);
  }
}

void_t ConnectionHandler::removedUserMarket(Market2& market)
{
  setUserMarketState(market, meguco_user_market_stopped);
  tables.remove(market.getTableId());
  market.getUser().deleteMarket(market);
}

void_t ConnectionHandler::removedUserSession(Session2& session)
{
  setUserSessionState(session, meguco_user_session_stopped);
  tables.remove(session.getTableId());
  session.getUser().deleteSession(session);
}
