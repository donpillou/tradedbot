
#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#endif

#include <nstd/Console.h>
#include <nstd/String.h>
#include <nstd/Thread.h> // sleep
#include <nstd/HashSet.h>
//#include <nstd/Error.h>

#include "Tools/BotConnection.h"
#include "Tools/HandlerConnection.h"

#ifdef MARKET_BITSTAMPUSD
#include "Markets/BitstampUsd.h"
typedef BitstampMarket MarketAdapter;
#endif

class BotConnectionHandler
{
public:

  BotConnectionHandler() : market(0) {}
  ~BotConnectionHandler()
  {
    delete market;
  }

  bool_t connect(uint16_t port)
  {
    if(market)
      return false;

    if(!handlerConnection.connect(port))
      return false;
    if(!botConnection.connect(port))
      return false;

    market = new MarketAdapter(handlerConnection.getUserName(), handlerConnection.getKey(), handlerConnection.getSecret());
    return true;
  }

  bool_t process()
  {
    BotProtocol::Header header;
    byte_t* data;
    size_t size;
    for(;;)
    {
      if(!handlerConnection.receiveMessage(header, data, size))
        return false;
      if(!handleMessage(header, data, size))
        return false;
    }
  }

  String getErrorString() const
  {
    String error = handlerConnection.getErrorString();
    if(!error.isEmpty())
      return error;
    return botConnection.getErrorString();
  }

private:
  HandlerConnection handlerConnection;
  BotConnection botConnection;
  Market* market;

  HashSet<uint32_t> orders;
  HashSet<uint32_t> transactions;

private:
  bool_t handleMessage(const BotProtocol::Header& header, byte_t* data, size_t size)
  {
    switch((BotProtocol::MessageType)header.messageType)
    {
    case BotProtocol::createEntity:
      if(size >= sizeof(BotProtocol::Entity))
        return handleCreateEntity(header.requestId, *(BotProtocol::Entity*)data, size);
      break;
    case BotProtocol::updateEntity:
      if(size >= sizeof(BotProtocol::Entity))
        return handleUpdateEntity(header.requestId, *(BotProtocol::Entity*)data, size);
      break;
    case BotProtocol::removeEntity:
      if(size >= sizeof(BotProtocol::Entity))
        return handleRemoveEntity(header.requestId, *(BotProtocol::Entity*)data);
      break;
    case BotProtocol::controlEntity:
      if(size >= sizeof(BotProtocol::Entity))
        return handleControlEntity(header.requestId, *(BotProtocol::Entity*)data, size);
      break;
    default:
      break;
    }
    return true;
  }

  bool_t handleCreateEntity(uint32_t requestId, BotProtocol::Entity& entity, size_t size)
  {
    switch((BotProtocol::EntityType)entity.entityType)
    {
    case BotProtocol::marketOrder:
      if(size >= sizeof(BotProtocol::Order))
        return handleCreateOrder(requestId, *(BotProtocol::Order*)&entity);
      break;
    default:
      break;
    }
    return true;
  }

  bool_t handleUpdateEntity(uint32_t requestId, BotProtocol::Entity& entity, size_t size)
  {
    switch((BotProtocol::EntityType)entity.entityType)
    {
    case BotProtocol::marketOrder:
      if(size >= sizeof(BotProtocol::Order))
        return handleUpdateOrder(requestId, *(BotProtocol::Order*)&entity);
      break;
    default:
      break;
    }
    return true;
  }

  bool_t handleRemoveEntity(uint32_t requestId, const BotProtocol::Entity& entity)
  {
    switch((BotProtocol::EntityType)entity.entityType)
    {
    case BotProtocol::marketOrder:
      return handleRemoveOrder(requestId, entity);
      break;
    default:
      break;
    }
    return true;
  }

  bool_t handleControlEntity(uint32_t requestId, BotProtocol::Entity& entity, size_t size)
  {
    switch((BotProtocol::EntityType)entity.entityType)
    {
    case BotProtocol::market:
      if(size >= sizeof(BotProtocol::ControlMarket))
        return handleControlMarket(requestId, *(BotProtocol::ControlMarket*)&entity);
      break;
    default:
      break;
    }
    return true;
  }

  bool_t handleCreateOrder(uint32_t requestId, BotProtocol::Order& createOrderArgs)
  {
    BotProtocol::Order order;
    if(!market->createOrder(0, (BotProtocol::Order::Type)createOrderArgs.type, createOrderArgs.price, createOrderArgs.amount, order))
      return handlerConnection.sendErrorResponse(BotProtocol::createEntity, requestId, &createOrderArgs, market->getLastError());
    order.timeout = createOrderArgs.timeout;

    if(!handlerConnection.sendMessage(BotProtocol::createEntityResponse, requestId, &order, sizeof(order)))
      return false;
    this->orders.append(order.entityId);
    return botConnection.sendEntity(&order, sizeof(order));
  }

  bool_t handleUpdateOrder(uint32_t requestId, BotProtocol::Order& updateOrderArgs)
  {
    // step #1 cancel current order
    if(!market->cancelOrder(updateOrderArgs.entityId))
      return handlerConnection.sendErrorResponse(BotProtocol::updateEntity, requestId, &updateOrderArgs, market->getLastError());

    // step #2 create new order with same id
    BotProtocol::Order order;
    if(!market->createOrder(updateOrderArgs.entityId, (BotProtocol::Order::Type)updateOrderArgs.type, updateOrderArgs.price, updateOrderArgs.amount, order))
    {
      if(!handlerConnection.sendErrorResponse(BotProtocol::updateEntity, requestId, &updateOrderArgs, market->getLastError()))
        return false;
      this->orders.remove(updateOrderArgs.entityId);
      if(!botConnection.removeEntity(BotProtocol::marketOrder, updateOrderArgs.entityId))
        return false;
      return true;
    }
    order.timeout = updateOrderArgs.timeout;
    if(!handlerConnection.sendMessage(BotProtocol::updateEntityResponse, requestId, &updateOrderArgs, sizeof(BotProtocol::Entity)))
      return false;
    this->orders.append(order.entityId);
    return botConnection.sendEntity(&order, sizeof(order));
  }

  bool_t handleRemoveOrder(uint32_t requestId, const BotProtocol::Entity& entity)
  {
    if(!market->cancelOrder(entity.entityId))
    {
      if(!handlerConnection.sendErrorResponse(BotProtocol::removeEntity, requestId, &entity, market->getLastError()))
        return false;
      BotProtocol::Order order;
      if(market->getOrder(entity.entityId, order))
        if(!botConnection.sendEntity(&order, sizeof(order)))
          return false;
      return true;
    }
    if(!handlerConnection.sendMessage(BotProtocol::removeEntityResponse, requestId, &entity, sizeof(entity)))
      return false;
    this->orders.remove(entity.entityId);
    return botConnection.removeEntity(BotProtocol::marketOrder, entity.entityId);
  }

  bool_t handleControlMarket(uint32_t requestId, BotProtocol::ControlMarket& controlMarket)
  {
    BotProtocol::ControlMarketResponse response;
    response.entityType = BotProtocol::market;
    response.entityId = controlMarket.entityId;
    response.cmd = controlMarket.cmd;

    switch((BotProtocol::ControlMarket::Command)controlMarket.cmd)
    {
    case BotProtocol::ControlMarket::refreshOrders:
    case BotProtocol::ControlMarket::requestOrders:
      {
        List<BotProtocol::Order> orders;
        if(!market->loadOrders(orders))
          return handlerConnection.sendErrorResponse(BotProtocol::controlEntity, requestId, &controlMarket, market->getLastError());
        if(controlMarket.cmd == BotProtocol::ControlMarket::refreshOrders)
        {
          if(!handlerConnection.sendMessage(BotProtocol::controlEntityResponse, requestId, &response, sizeof(response)))
            return false;
        }
        else
        {
          size_t dataSize = sizeof(response) + sizeof(BotProtocol::Order) * orders.size();
          if(!handlerConnection.sendMessageHeader(BotProtocol::controlEntityResponse, requestId, dataSize) ||
             !handlerConnection.sendMessageData(&response, sizeof(response)))
            return false;
          for(List<BotProtocol::Order>::Iterator i = orders.begin(), end = orders.end(); i != end; ++i)
            if(!handlerConnection.sendMessageData(&*i, sizeof(BotProtocol::Order)))
              return false;
        }
        HashSet<uint32_t> ordersToRemove;
        ordersToRemove.swap(this->orders);
        for(List<BotProtocol::Order>::Iterator i = orders.begin(), end = orders.end(); i != end; ++i)
        {
          this->orders.append(i->entityId);
          if(!botConnection.sendEntity(&*i, sizeof(BotProtocol::Order)))
            return false;
          ordersToRemove.remove(i->entityId);
        }
        for(HashSet<uint32_t>::Iterator i = ordersToRemove.begin(), end = ordersToRemove.end(); i != end; ++i)
          if(!botConnection.removeEntity(BotProtocol::marketOrder, *i))
            return false;
      }
      break;
    case BotProtocol::ControlMarket::refreshTransactions:
    case BotProtocol::ControlMarket::requestTransactions:
      {
        List<BotProtocol::Transaction> transactions;
        if(!market->loadTransactions(transactions))
          return handlerConnection.sendErrorResponse(BotProtocol::controlEntity, requestId, &controlMarket, market->getLastError());
        if(controlMarket.cmd == BotProtocol::ControlMarket::refreshTransactions)
        {
          if(!handlerConnection.sendMessage(BotProtocol::controlEntityResponse, requestId, &response, sizeof(response)))
              return false;
        }
        else
        {
          size_t dataSize = sizeof(response) + sizeof(BotProtocol::Transaction) * transactions.size();
          if(!handlerConnection.sendMessageHeader(BotProtocol::controlEntityResponse, requestId, dataSize) ||
             !handlerConnection.sendMessageData(&response, sizeof(response)))
            return false;
          for(List<BotProtocol::Transaction>::Iterator i = transactions.begin(), end = transactions.end(); i != end; ++i)
            if(!handlerConnection.sendMessageData(&*i, sizeof(BotProtocol::Transaction)))
              return false;
        }
        HashSet<uint32_t> transactionsToRemove;
        transactionsToRemove.swap(this->transactions);
        for(List<BotProtocol::Transaction>::Iterator i = transactions.begin(), end = transactions.end(); i != end; ++i)
        {
          this->transactions.append(i->entityId);
          if(!botConnection.sendEntity(&*i, sizeof(BotProtocol::Transaction)))
            return false;
          transactionsToRemove.remove(i->entityId);
        }
        for(HashSet<uint32_t>::Iterator i = transactionsToRemove.begin(), end = transactionsToRemove.end(); i != end; ++i)
          if(!botConnection.removeEntity(BotProtocol::marketTransaction, *i))
            return false;
      }
      break;
    case BotProtocol::ControlMarket::refreshBalance:
    case BotProtocol::ControlMarket::requestBalance:
      {
        BotProtocol::Balance balance;
        if(!market->loadBalance(balance))
          return handlerConnection.sendErrorResponse(BotProtocol::controlEntity, requestId, &controlMarket, market->getLastError());
        if(controlMarket.cmd == BotProtocol::ControlMarket::refreshBalance)
        {
          if(!handlerConnection.sendMessage(BotProtocol::controlEntityResponse, requestId, &response, sizeof(response)))
            return false;
        }
        else
        {
          size_t dataSize = sizeof(response) + sizeof(balance);
          if(!handlerConnection.sendMessageHeader(BotProtocol::controlEntityResponse, requestId, dataSize) ||
             !handlerConnection.sendMessageData(&response, sizeof(response)) ||
             !handlerConnection.sendMessageData(&balance, sizeof(balance)))
            return false;
        }
        if(!botConnection.sendEntity(&balance, sizeof(balance)))
          return false;
      }
      break;
    default:
      break;
    }
    return true;
  }
};

int_t main(int_t argc, char_t* argv[])
{
  static const uint16_t port = 40124;

  //for(;;)
  //{
  //  bool stop = true;
  //  if(!stop)
  //    break;
  //}

  // create connection to bot server
  BotConnectionHandler connection;
  if(!connection.connect(port))
  {
    Console::errorf("error: Could not connect to bot server: %s\n", (const char_t*)connection.getErrorString());
    return -1;
  }

  // wait for requests
  if(!connection.process())
  {
    Console::errorf("error: Lost connection to bot server: %s\n", (const char_t*)connection.getErrorString());
    return -1;
  }

  return 0;
}
