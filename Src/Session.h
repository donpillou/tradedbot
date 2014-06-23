

#pragma once

#include <nstd/String.h>
#include <nstd/Process.h>
#include <nstd/Variant.h>
#include <nstd/HashSet.h>

#include "BotProtocol.h"

class ServerHandler;
class ClientHandler;
class BotEngine;
class Market;
class Transaction;
class User;
class Order;

class Session
{
public:
  Session(ServerHandler& serverHandler, User& user, uint32_t id, const String& name, BotEngine& engine, Market& market, double initialBalanceBase, double initialBalanceComm);
  Session(ServerHandler& serverHandler, User& user, const Variant& variant);
  ~Session();
  void_t toVariant(Variant& variant);

  bool_t saveData();

  bool_t startSimulation();
  bool_t startLive();
  bool_t stop();

  bool_t registerClient(ClientHandler& client, bool_t bot);
  void_t unregisterClient(ClientHandler& client);

  User& getUser() {return user;}
  uint32_t getId() const {return __id;}
  const String& getName() const {return name;}
  BotEngine* getEngine() const {return engine;}
  Market* getMarket() const {return market;}
  BotProtocol::Session::State getState() const {return state;}
  bool isSimulation() const {return simulation;}
  const BotProtocol::Balance& getBalance() const {return balance;}

  BotProtocol::Transaction* createTransaction(const BotProtocol::Transaction& transaction);
  BotProtocol::Transaction* updateTransaction(const BotProtocol::Transaction& transaction);
  const HashMap<uint32_t, BotProtocol::Transaction>& getTransactions() const {return transactions;}
  bool_t deleteTransaction(uint32_t id);

  BotProtocol::SessionItem* createItem(const BotProtocol::SessionItem& item);
  BotProtocol::SessionItem* updateItem(const BotProtocol::SessionItem& item);
  const HashMap<uint32_t, BotProtocol::SessionItem>& getItems() const {return items;}
  bool_t deleteItem(uint32_t id);

  BotProtocol::Order* createOrder(const BotProtocol::Order& order);
  void_t updateOrder(const BotProtocol::Order& order) {orders.append(order.entityId, order);}
  const HashMap<uint32_t, BotProtocol::Order>& getOrders() const {return orders;}
  bool_t deleteOrder(uint32_t id);

  BotProtocol::Marker* createMarker(const BotProtocol::Marker& marker);
  const HashMap<uint32_t, BotProtocol::Marker>& getMarkers() const {return markers;}

  BotProtocol::SessionLogMessage* addLogMessage(const BotProtocol::SessionLogMessage& logMessage);
  const List<BotProtocol::SessionLogMessage>& getLogMessages() const {return logMessages;}

  void_t updateBalance(const BotProtocol::Balance& balance) {this->balance = balance;}

  void_t getEntity(BotProtocol::Session& session) const;
  void_t sendUpdateEntity(const void_t* data, size_t size);
  void_t sendRemoveEntity(BotProtocol::EntityType type, uint32_t id);
  void_t sendRemoveAllEntities(BotProtocol::EntityType type);

private:
  ServerHandler& serverHandler;
  User& user;
  uint32_t __id;
  String name;
  BotEngine* engine;
  Market* market;
  bool simulation;
  double initialBalanceBase;
  double initialBalanceComm;
  BotProtocol::Balance balance;
  BotProtocol::Session::State state;
  Process process;
  uint32_t pid;
  ClientHandler* botClient;
  HashSet<ClientHandler*> clients;
  HashMap<uint32_t, BotProtocol::Transaction> transactions;
  HashMap<uint32_t, BotProtocol::SessionItem> items;
  HashMap<uint32_t, BotProtocol::Order> orders;
  HashMap<uint32_t, BotProtocol::Marker> markers;
  List<BotProtocol::SessionLogMessage> logMessages;
  BotProtocol::Balance backupBalance;
  HashMap<uint32_t, BotProtocol::Transaction> backupTransactions;
  HashMap<uint32_t, BotProtocol::SessionItem> backupItems;
  HashMap<uint32_t, BotProtocol::Order> backupOrders;
  HashMap<uint32_t, BotProtocol::Marker> backupMarkers;
  List<BotProtocol::SessionLogMessage> backupLogMessages;
  uint32_t nextEntityId;
};
