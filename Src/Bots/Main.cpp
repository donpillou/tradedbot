
#include <nstd/Console.h>
#include <nstd/Log.h>
#include <nstd/Thread.h>
#include <nstd/Time.h>
#include <zlimdbprotocol.h>
#include <megucoprotocol.h>

#include "Tools/Broker.h"
#include "Tools/SimBroker.h"
#include "Tools/LiveBroker.h"

#include "Main.h"

int_t main(int_t argc, char_t* argv[])
{
  if(argc < 3)
  {
    Console::errorf("error: Missing session attributes\n");
    return -1;
  }
  String userName(argv[1], String::length(argv[1]));
  uint64_t sessionId = String::toUInt64(argv[2]);

  Log::setFormat("%P> %m");

  //bool stop = true;
  //while(stop);

  Main main;
  for(;; Thread::sleep(10 * 1000))
  {
    if(!main.connect(userName, sessionId))
    {
      Log::errorf("error: Could not connect to zlimdb server: %s", (const tchar_t*)main.getErrorString());
      continue;
    }
    Log::infof("Connected to zlimdb server.");

    main.process();
    Log::errorf("Lost connection to zlimdb server: %s", (const char_t*)main.getErrorString());
  }

  return 0;
}
#ifdef BOT_BETBOT
#include "Bots/BetBot.h"
typedef BetBot BotFactory;
#endif
#ifdef BOT_BETBOT2
#include "Bots/BetBot2.h"
typedef BetBot2 BotFactory;
#endif
#ifdef BOT_FLIPBOT
#include "Bots/FlipBot.h"
typedef FlipBot BotFactory;
#endif
#ifdef BOT_TESTBOT
#include "Bots/TestBot.h"
typedef TestBot BotFactory;
#endif

Main::~Main() {delete broker;}

bool_t Main::connect(const String& userName, uint64_t sessionId)
{
  // close current connections
  delete broker;
  broker = 0;
  delete botSession;
  botSession = 0;

  // create session entity connection
  if(!connection.connect(*this))
    return false;

  // subscribe to user session table
  String tablePrefix = String("users/") + userName + "/sessions/" + String::fromUInt64(sessionId);
  if(!connection.createTable(tablePrefix + "/session", sessionTableId))
    return false;
  if(!connection.subscribe(sessionTableId, zlimdb_subscribe_flag_responder))
    return false;
  simulation = true;
  uint32_t brokerId = 0;
  byte_t buffer[ZLIMDB_MAX_MESSAGE_SIZE];
  while(connection.getResponse(buffer))
  {
    for(const meguco_user_session_entity* userBrokerEntity = (const meguco_user_session_entity*)zlimdb_get_first_entity((const zlimdb_header*)buffer, sizeof(meguco_user_session_entity));
      userBrokerEntity;
      userBrokerEntity = (const meguco_user_session_entity*)zlimdb_get_next_entity((const zlimdb_header*)buffer, sizeof(meguco_user_session_entity), &userBrokerEntity->entity))
    {
      if(userBrokerEntity->entity.id != 1)
        continue;
      if(userBrokerEntity->mode == meguco_user_session_live)
        simulation = false;
      brokerId = userBrokerEntity->broker_id;
    }
  }
  if(connection.getErrno() != 0)
    return false;

  // get table ids (or create tables in case they do not exist)
  if(!connection.createTable(tablePrefix + "/transactions", transactionsTableId))
    return false;
  if(!connection.createTable(tablePrefix + "/assets", assetsTableId))
    return false;
  if(!connection.createTable(tablePrefix + "/orders", ordersTableId))
    return false;
  if(!connection.createTable(tablePrefix + "/log", logTableId))
    return false;
  if(!connection.createTable(tablePrefix + "/properties", propertiesTableId))
    return false;
  if(!connection.createTable(tablePrefix + "/markers", markersTableId))
    return false;

  // get broker
  String brokerTablePrefix = String("users/") + userName + "/brokers/" + String::fromUInt64(brokerId);
  if(!connection.findTable(brokerTablePrefix + "/broker", brokerTableId))
    return false;
  if(!connection.queryEntity(brokerTableId, 1, *(zlimdb_entity*)buffer, sizeof(meguco_user_broker_entity), ZLIMDB_MAX_ENTITY_SIZE))
    return false;
  const meguco_user_broker_entity* brokerEntity = (const meguco_user_broker_entity*)buffer;
  uint64_t brokerTypeId = brokerEntity->broker_type_id;

  // get broker type info
  uint32_t brokersTableId;
  if(!connection.findTable("brokers", brokersTableId))
    return false;
  meguco_broker_type_entity* brokerType = (meguco_broker_type_entity*)buffer;
  if(!connection.queryEntity(brokersTableId, brokerTypeId, brokerType->entity, sizeof(meguco_broker_type_entity), sizeof(buffer)))
    return false;
  String marketName;
  if(!ZlimdbConnection::getString(brokerType->entity, sizeof(meguco_broker_type_entity), brokerType->name_size, marketName))
    return false;
  size_t pos = 0;
  marketName.detach();
  marketName.token('/', pos);
  String currencyComm = marketName.token('/', pos);
  String currencyBase = marketName.token('/', pos);

  // get broker orders table id
  if(!connection.findTable(brokerTablePrefix + "/orders", brokerOrdersTableId))
    return false;

  // create local broker interface
  BotFactory botFactory;
  maxTradeAge = botFactory.getMaxTradeAge();
  if(simulation)
  {
    // get broker balance
    meguco_user_broker_balance_entity balance;
    if(!getBrokerBalance(balance))
      return false;
    broker = new SimBroker(*this, currencyBase, currencyComm, balance.fee, maxTradeAge);
  }
  else
    broker = new LiveBroker(*this, currencyBase, currencyComm);

  // get session orders
  if(!connection.query(ordersTableId))
    return false;
  while(connection.getResponse(buffer))
  {
    for(const meguco_user_broker_order_entity* order = (const meguco_user_broker_order_entity*)zlimdb_get_first_entity((const zlimdb_header*)buffer, sizeof(meguco_user_broker_order_entity));
      order;
      order = (const meguco_user_broker_order_entity*)zlimdb_get_next_entity((const zlimdb_header*)buffer, sizeof(meguco_user_broker_order_entity), &order->entity))
      broker->registerOrder(*order);
  }
  if(connection.getErrno() != 0)
    return false;

  // get session properties
  if(!connection.query(propertiesTableId))
    return false;
  String name, value, unit;
  while(connection.getResponse(buffer))
  {
    for(const meguco_user_session_property_entity* property = (const meguco_user_session_property_entity*)zlimdb_get_first_entity((const zlimdb_header*)buffer, sizeof(meguco_user_session_property_entity));
      property;
      property = (const meguco_user_session_property_entity*)zlimdb_get_next_entity((const zlimdb_header*)buffer, sizeof(meguco_user_session_property_entity), &property->entity))
    {
      size_t offset = sizeof(meguco_user_session_property_entity);
      if(!ZlimdbConnection::getString(property->entity, property->name_size, name, offset) ||
        !ZlimdbConnection::getString(property->entity, property->value_size, value, offset) ||
        !ZlimdbConnection::getString(property->entity, property->unit_size, unit, offset))
        continue;
      broker->registerProperty(Bot::Property(*property, name, value, unit));
    }
  }
  if(connection.getErrno() != 0)
    return false;

  // get live properties table
  if(simulation)
  {
    if(!connection.findTable(tablePrefix + "/properties.backup", livePropertiesTableId))
      return false;
    if(!connection.query(livePropertiesTableId))
      return false;
    while(connection.getResponse(buffer))
    {
      String name, value, unit;
      for(const meguco_user_session_property_entity* property = (const meguco_user_session_property_entity*)zlimdb_get_first_entity((const zlimdb_header*)buffer, sizeof(meguco_user_session_property_entity));
        property;
        property = (const meguco_user_session_property_entity*)zlimdb_get_next_entity((const zlimdb_header*)buffer, sizeof(meguco_user_session_property_entity), &property->entity))
      {
        size_t offset = sizeof(meguco_user_session_property_entity);
        if(!ZlimdbConnection::getString(property->entity, property->name_size, name, offset))
          continue;
        liveProperties.append(name);
      }
    }
    if(connection.getErrno() != 0)
      return false;
  }

  // get session assets
  if(!connection.query(assetsTableId))
    return false;
  while(connection.getResponse(buffer))
  {
    for(const meguco_user_session_asset_entity* asset = (const meguco_user_session_asset_entity*)zlimdb_get_first_entity((const zlimdb_header*)buffer, sizeof(meguco_user_session_asset_entity));
      asset;
      asset = (const meguco_user_session_asset_entity*)zlimdb_get_next_entity((const zlimdb_header*)buffer, sizeof(meguco_user_session_asset_entity), &asset->entity))
      broker->registerAsset(*asset);
  }
  if(connection.getErrno() != 0)
    return false;

  // instantiate bot implementation
  botSession = botFactory.createSession(*broker);

  // get trade history
  if(!connection.findTable(String("markets/") + marketName + "/trades", tradesTableId))
    return false;
  int64_t serverTime, tableTime;
  if(!connection.sync(tradesTableId, serverTime, tableTime))
    return false;
  if(!connection.subscribe(tradesTableId, zlimdb_query_type_since_time, tableTime - (simulation ? (7ULL * 24ULL * 60ULL * 60ULL * 1000ULL) : (maxTradeAge + 10 * 60 * 1000)), zlimdb_subscribe_flag_none))
    return false;
  while(connection.getResponse(buffer))
  {
    for(const meguco_trade_entity* trade = (const meguco_trade_entity*)zlimdb_get_first_entity((const zlimdb_header*)buffer, sizeof(meguco_trade_entity));
      trade;
      trade = (const meguco_trade_entity*)zlimdb_get_next_entity((const zlimdb_header*)buffer, sizeof(meguco_trade_entity), &trade->entity))
      broker->handleTrade(*botSession, *trade, true);
  }
  if(connection.getErrno() != 0)
    return false;

  if(simulation)
    addLogMessage(Time::time(), "synced");

  return true;
}

bool_t Main::getBrokerBalance(meguco_user_broker_balance_entity& balance)
{
  byte_t buffer[ZLIMDB_MAX_MESSAGE_SIZE];
  if(!connection.control(brokerTableId, 1, meguco_user_broker_control_refresh_balance, 0, 0, buffer))
    return false;
  const meguco_user_broker_balance_entity* balanceEntity = (const meguco_user_broker_balance_entity*)zlimdb_get_first_entity((const zlimdb_header*)buffer, sizeof(meguco_user_broker_balance_entity));
  if(!balanceEntity)
  {
    connection.setErrno(zlimdb_local_error_invalid_message_data);
    return false;
  }
  balance = *balanceEntity;
  return true;
}

bool_t Main::getBrokerOrders(List<meguco_user_broker_order_entity>& orders)
{
  byte_t buffer[ZLIMDB_MAX_MESSAGE_SIZE];
  if(!connection.control(brokerTableId, 1, meguco_user_broker_control_refresh_orders, 0, 0, buffer))
    return false;
  if(!connection.query(brokerOrdersTableId))
    return false;
  orders.clear();
  while(connection.getResponse(buffer))
    for(const meguco_user_broker_order_entity* order = (const meguco_user_broker_order_entity*)zlimdb_get_first_entity((const zlimdb_header*)buffer, sizeof(meguco_user_broker_order_entity));
        order;
        order = (const meguco_user_broker_order_entity*)zlimdb_get_next_entity((const zlimdb_header*)buffer, sizeof(meguco_user_broker_order_entity), &order->entity))
      orders.append(*order);
  if(connection.getErrno() != 0)
    return false;
  return true;
}

bool_t Main::createBrokerOrder2(Bot::Order& order)
{
  meguco_user_broker_order_entity orderEntity = order;
  byte_t buffer[ZLIMDB_MAX_MESSAGE_SIZE];
  if(!connection.control(brokerTableId, 1, meguco_user_broker_control_create_order, &orderEntity, sizeof(meguco_user_broker_order_entity), buffer))
    return false;
  const meguco_user_broker_order_entity* response = (const meguco_user_broker_order_entity*)zlimdb_get_response_data((const zlimdb_header*)buffer, sizeof(meguco_user_broker_order_entity));
  if(!response)
    return false;
  order = *response;
  return true;
}

bool_t Main::removeBrokerOrder(uint64_t id)
{
  byte_t buffer[ZLIMDB_MAX_MESSAGE_SIZE];
  if(!connection.control(brokerTableId, id, meguco_user_broker_control_remove_order, 0, 0, buffer))
    return false;
  return true;
}

bool_t Main::createSessionTransaction(Bot::Transaction& transaction)
{
  meguco_user_broker_transaction_entity transactionEntity = transaction;
  if(!connection.add(transactionsTableId, transactionEntity.entity, transaction.id))
    return false;
  return true;
}

bool_t Main::createSessionOrder(Bot::Order& order)
{
  meguco_user_broker_order_entity orderEntity = order;
  if(!connection.add(ordersTableId, orderEntity.entity, order.id))
    return false;
  return true;
}

bool_t Main::removeSessionOrder(uint64_t id)
{
  if(!connection.remove(ordersTableId, id))
    return false;
  return true;
}

bool_t Main::createSessionAsset(Bot::Asset& asset)
{
  meguco_user_session_asset_entity newAsset = asset;
  if(!connection.add(assetsTableId, newAsset.entity, asset.id))
    return false;
  return true;
}

bool_t Main::updateSessionAsset(const Bot::Asset& asset)
{
  meguco_user_session_asset_entity updatedAsset = asset;
  if(!connection.update(assetsTableId, updatedAsset.entity))
    return false;
  return true;
}

bool_t Main::removeSessionAsset(uint64_t id)
{
  if(!connection.remove(assetsTableId, id))
    return false;
  return true;
}

bool_t Main::createSessionMarker(Bot::Marker& marker)
{
  meguco_user_session_marker_entity markerEntity = marker;
  if(!connection.add(markersTableId, markerEntity.entity, marker.id))
    return false;
  return true;
}

bool_t Main::createSessionProperty(Bot::Property& property)
{
  byte_t buffer[ZLIMDB_MAX_ENTITY_SIZE];
  meguco_user_session_property_entity* newProperty = (meguco_user_session_property_entity*)buffer;
  ZlimdbConnection::setEntityHeader(newProperty->entity, 0, 0, sizeof(meguco_user_session_property_entity));
  newProperty->flags = property.flags;
  newProperty->type = property.type;
  if(!ZlimdbConnection::copyString(property.name, newProperty->entity, newProperty->name_size, ZLIMDB_MAX_ENTITY_SIZE) ||
    !ZlimdbConnection::copyString(property.value, newProperty->entity, newProperty->value_size, ZLIMDB_MAX_ENTITY_SIZE) ||
    !ZlimdbConnection::copyString(property.unit, newProperty->entity, newProperty->unit_size, ZLIMDB_MAX_ENTITY_SIZE))
    return false;
  if(!connection.add(propertiesTableId, newProperty->entity, property.id))
    return false;

  if(simulation)
  {
    if(!liveProperties.contains(property.name))
    {
      uint64_t id;
      if(!connection.add(livePropertiesTableId, newProperty->entity, id))
        return false;
      liveProperties.append(property.name);
    }
  }

  return true;
}

bool_t Main::updateSessionProperty(const Bot::Property& property)
{
  byte_t buffer[ZLIMDB_MAX_ENTITY_SIZE];
  meguco_user_session_property_entity* updatedProperty = (meguco_user_session_property_entity*)buffer;
  *updatedProperty = property;
  updatedProperty->entity.size = sizeof(meguco_user_session_property_entity);
  if(!ZlimdbConnection::copyString(property.name, updatedProperty->entity, updatedProperty->name_size, ZLIMDB_MAX_ENTITY_SIZE) ||
    !ZlimdbConnection::copyString(property.value, updatedProperty->entity, updatedProperty->value_size, ZLIMDB_MAX_ENTITY_SIZE) ||
    !ZlimdbConnection::copyString(property.unit, updatedProperty->entity, updatedProperty->unit_size, ZLIMDB_MAX_ENTITY_SIZE))
    return false;
  if(!connection.update(propertiesTableId, updatedProperty->entity))
    return false;
  return true;
}

bool_t Main::removeSessionProperty(uint64_t id)
{
  if(!connection.remove(propertiesTableId, id))
    return false;
  return true;
}

bool_t Main::addLogMessage(int64_t time, const String& message)
{
  byte_t buffer[ZLIMDB_MAX_ENTITY_SIZE];
  meguco_log_entity* log = (meguco_log_entity*)buffer;
  ZlimdbConnection::setEntityHeader(log->entity, 0, 0, sizeof(meguco_log_entity));
  log->type = meguco_log_info;
  log->time = time;
  if(!ZlimdbConnection::copyString(message, log->entity, log->message_size, ZLIMDB_MAX_ENTITY_SIZE))
    return false;
  if(!connection.add(logTableId, log->entity, log->entity.id))
    return false;
  return true;
}

void_t Main::controlEntity(uint32_t tableId, uint32_t requestId, uint64_t entityId, uint32_t controlCode, const byte_t* data, size_t size)
{
  if(tableId == sessionTableId)
    return controlUserSession(requestId, entityId, controlCode, data, size);
  else
    return (void_t)connection.sendControlResponse(requestId, zlimdb_error_invalid_request);
}

void_t Main::addedEntity(uint32_t tableId, const zlimdb_entity& entity)
{
  if(tableId == tradesTableId)
  {
    if(entity.size < sizeof(meguco_trade_entity))
      return;
    const meguco_trade_entity* trade = (const meguco_trade_entity*)&entity;
    broker->handleTrade(*botSession, *trade, false);
  }
}

void_t Main::controlUserSession(uint32_t requestId, uint64_t entityId, uint32_t controlCode, const byte_t* data, size_t size)
{
  switch(controlCode)
  {
  case meguco_user_session_control_create_asset:
    {
      const meguco_user_session_asset_entity* asset = (const meguco_user_session_asset_entity*)zlimdb_data_get_first_entity(data, size, sizeof(meguco_user_session_asset_entity));
      if(!asset)
        return (void_t)connection.sendControlResponse(requestId, zlimdb_error_invalid_request);
      meguco_user_session_asset_entity newAsset = *asset;
      if(newAsset.state == meguco_user_session_asset_submitting)
        newAsset.state = asset->type == meguco_user_session_asset_buy ? meguco_user_session_asset_wait_buy : meguco_user_session_asset_wait_sell;
      if(!connection.add(assetsTableId, newAsset.entity, newAsset.entity.id))
        return (void_t)connection.sendControlResponse(requestId, (uint16_t)connection.getErrno());
      Bot::Asset asset2 = newAsset;
      broker->registerAsset(asset2);
      botSession->handleAssetUpdate(asset2);
      return (void_t)connection.sendControlResponse(requestId, (const byte_t*)&newAsset.entity.id, sizeof(uint64_t));
    }

  case meguco_user_session_control_remove_asset:
    {
      const Bot::Asset* asset = broker->getAsset(entityId);
      if(!asset)
        return (void_t)connection.sendControlResponse(requestId, zlimdb_error_entity_not_found);
      if(!connection.remove(assetsTableId, entityId))
        return (void_t)connection.sendControlResponse(requestId, (uint16_t)connection.getErrno());
      broker->unregisterAsset(entityId);
      botSession->handleAssetRemoval(*asset);
      return (void_t)connection.sendControlResponse(requestId, 0, 0);
    }

  case meguco_user_session_control_update_asset:
    {
      if(size < sizeof(meguco_user_session_control_update_asset_params))
        return (void_t)connection.sendControlResponse(requestId, zlimdb_error_invalid_request);
      const meguco_user_session_control_update_asset_params* args = (const meguco_user_session_control_update_asset_params*)data;
      const Bot::Asset* asset = broker->getAsset(entityId);
      if(!asset)
        return (void_t)connection.sendControlResponse(requestId, zlimdb_error_entity_not_found);
      meguco_user_session_asset_entity updatedAsset = *asset;
      updatedAsset.flip_price = args->flip_price;
      if(!connection.update(assetsTableId, updatedAsset.entity))
        return (void_t)connection.sendControlResponse(requestId, (uint16_t)connection.getErrno());
      Bot::Asset updatedAsset2 = updatedAsset;
      broker->registerAsset(updatedAsset2);
      botSession->handleAssetUpdate(updatedAsset2);
      return (void_t)connection.sendControlResponse(requestId, 0, 0);
    }

  case meguco_user_session_control_update_property:
    {
      // get args
      if(size < sizeof(meguco_user_session_control_update_property_params))
        return (void_t)connection.sendControlResponse(requestId, zlimdb_error_invalid_request);
      const meguco_user_session_control_update_property_params* args = (const meguco_user_session_control_update_property_params*)data;
      String newValue;
      if(!ZlimdbConnection::getString(args, size, sizeof(meguco_user_session_control_update_property_params), args->value_size, newValue))
        return (void_t)connection.sendControlResponse(requestId, zlimdb_error_invalid_request);

      // find property
      const Bot::Property* property = broker->getProperty(entityId);
      if(!property)
        return (void_t)connection.sendControlResponse(requestId, zlimdb_error_invalid_request);
      if(property->flags & meguco_user_session_property_read_only)
        return (void_t)connection.sendControlResponse(requestId, zlimdb_error_invalid_request);

      // create new property entity
      byte_t buffer[ZLIMDB_MAX_ENTITY_SIZE];
      meguco_user_session_property_entity* updatedProperty = (meguco_user_session_property_entity*)buffer;
      *updatedProperty = *property;
      updatedProperty->entity.size = sizeof(meguco_user_session_property_entity);
      if(!ZlimdbConnection::copyString(property->name, updatedProperty->entity, updatedProperty->name_size, ZLIMDB_MAX_ENTITY_SIZE) ||
        !ZlimdbConnection::copyString(newValue, updatedProperty->entity, updatedProperty->value_size, ZLIMDB_MAX_ENTITY_SIZE) ||
        !ZlimdbConnection::copyString(property->unit, updatedProperty->entity, updatedProperty->unit_size, ZLIMDB_MAX_ENTITY_SIZE))
        return (void_t)connection.sendControlResponse(requestId, zlimdb_error_invalid_request);

      // update property
      if(!connection.update(propertiesTableId, updatedProperty->entity))
        return (void_t)connection.sendControlResponse(requestId, (uint16_t)connection.getErrno());
      Bot::Property updatedProperty2(*updatedProperty, property->name, newValue, property->unit);
      broker->registerProperty(updatedProperty2);
      botSession->handlePropertyUpdate(updatedProperty2);

      return (void_t)connection.sendControlResponse(requestId, 0, 0);
    }

  default:
    return (void_t)connection.sendControlResponse(requestId, zlimdb_error_invalid_request);
  }
}
