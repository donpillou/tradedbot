
#include <nstd/File.h>
#include <nstd/Debug.h>
#include <nstd/Math.h>

#include "Tools/Sha256.h"
#include "ClientHandler.h"
#include "ServerHandler.h"
#include "User.h"
#include "Session.h"
#include "BotEngine.h"
#include "MarketAdapter.h"
#include "Market.h"

ClientHandler::ClientHandler(uint32_t clientAddr, ServerHandler& serverHandler, Server::Client& client) :
  clientAddr(clientAddr), serverHandler(serverHandler), client(client),
  state(newState), user(0), session(0), market(0) {}

ClientHandler::~ClientHandler()
{
  if(user)
    user->unregisterClient(*this);
  if(session)
  {
    session->unregisterClient(*this);
    if(state == botHandlerState)
    {
      BotProtocol::Session sessionEntity;
      session->getEntity(sessionEntity);
      session->getUser().sendUpdateEntity(&sessionEntity, sizeof(sessionEntity));
    }
  }
  if(market)
  {
    market->unregisterClient(*this);
    if(state == marketHandlerState)
    {
      BotProtocol::Market marketEntity;
      market->getEntity(marketEntity);
      market->getUser().sendUpdateEntity(&marketEntity, sizeof(marketEntity));
    }
  }
}

void_t ClientHandler::deselectSession()
{
  session = 0;
  if(state == botState)
    client.close();
}

void_t ClientHandler::deselectMarket()
{
  market = 0;
  if(state == marketState)
    client.close();
}

size_t ClientHandler::handle(byte_t* data, size_t size)
{
  byte_t* pos = data;
  while(size > 0)
  {
    if(size < sizeof(BotProtocol::Header))
      break;
    BotProtocol::Header* header = (BotProtocol::Header*)pos;
    if(header->size < sizeof(BotProtocol::Header) || header->size >= 5000)
    {
      client.close();
      return 0;
    }
    if(size < header->size)
      break;
    handleMessage(*header, pos + sizeof(BotProtocol::Header), header->size - sizeof(BotProtocol::Header));
    pos += header->size;
    size -= header->size;
  }
  if(size >= 5000)
  {
    client.close();
    return 0;
  }
  return pos - data;
}

void_t ClientHandler::handleMessage(const BotProtocol::Header& header, byte_t* data, size_t size)
{
  switch(state)
  {
  case newState:
    switch((BotProtocol::MessageType)header.messageType)
    {
    case BotProtocol::loginRequest:
      if(size >= sizeof(BotProtocol::LoginRequest))
        handleLogin(header.requestId, *(BotProtocol::LoginRequest*)data);
      break;
    case BotProtocol::registerBotRequest:
      if(clientAddr == Socket::loopbackAddr && size >= sizeof(BotProtocol::RegisterBotRequest))
        handleRegisterBot(header.requestId, *(BotProtocol::RegisterBotRequest*)data);
      break;
    case BotProtocol::registerBotHandlerRequest:
      if(clientAddr == Socket::loopbackAddr && size >= sizeof(BotProtocol::RegisterBotHandlerRequest))
        handleRegisterBotHandler(header.requestId, *(BotProtocol::RegisterBotHandlerRequest*)data);
      break;
    case BotProtocol::registerMarketRequest:
      if(clientAddr == Socket::loopbackAddr && size >= sizeof(BotProtocol::RegisterMarketRequest))
        handleRegisterMarket(header.requestId, *(BotProtocol::RegisterMarketRequest*)data);
      break;
    case BotProtocol::registerMarketHandlerRequest:
      if(clientAddr == Socket::loopbackAddr && size >= sizeof(BotProtocol::RegisterMarketHandlerRequest))
        handleRegisterMarketHandler(header.requestId, *(BotProtocol::RegisterMarketHandlerRequest*)data);
      break;
    default:
      break;
    }
    break;
  case loginState:
    if((BotProtocol::MessageType)header.messageType == BotProtocol::authRequest)
      if(size >= sizeof(BotProtocol::AuthRequest))
        handleAuth(header.requestId, *(BotProtocol::AuthRequest*)data);
    break;
  case userState:
  case botState:
    switch((BotProtocol::MessageType)header.messageType)
    {
    case BotProtocol::pingRequest:
      handlePing(data, size);
      break;
    case BotProtocol::createEntity:
      if(size >= sizeof(BotProtocol::Entity))
        handleCreateEntity(header.requestId, *(BotProtocol::Entity*)data, size);
      break;
    case BotProtocol::controlEntity:
      if(size >= sizeof(BotProtocol::Entity))
        handleControlEntity(header.requestId, *(BotProtocol::Entity*)data, size);
      break;
    case BotProtocol::updateEntity:
      if(size >= sizeof(BotProtocol::Entity))
        handleUpdateEntity(header.requestId, *(BotProtocol::Entity*)data, size);
      break;
    case BotProtocol::removeEntity:
      if(size >= sizeof(BotProtocol::Entity))
        handleRemoveEntity(header.requestId, *(const BotProtocol::Entity*)data);
      break;
    default:
      break;
    }
    break;
  case marketState:
    switch((BotProtocol::MessageType)header.messageType)
    {
    case BotProtocol::pingRequest:
      handlePing(data, size);
      break;
    case BotProtocol::updateEntity:
      if(size >= sizeof(BotProtocol::Entity))
        handleUpdateEntity(header.requestId, *(BotProtocol::Entity*)data, size);
      break;
    case BotProtocol::removeEntity:
      if(size >= sizeof(BotProtocol::Entity))
        handleRemoveEntity(header.requestId, *(const BotProtocol::Entity*)data);
      break;
    default:
      break;
    }
  case marketHandlerState:
  case botHandlerState:
    switch((BotProtocol::MessageType)header.messageType)
    {
    case BotProtocol::createEntityResponse:
    case BotProtocol::updateEntityResponse:
    case BotProtocol::removeEntityResponse:
    case BotProtocol::controlEntityResponse:
      if(size >= sizeof(BotProtocol::Entity))
        handleResponse((BotProtocol::MessageType)header.messageType, header.requestId, *(const BotProtocol::Entity*)data, size);
      break;
    case BotProtocol::errorResponse:
      if(size >= sizeof(BotProtocol::ErrorResponse))
        handleResponse((BotProtocol::MessageType)header.messageType, header.requestId, *(const BotProtocol::ErrorResponse*)data, sizeof(BotProtocol::ErrorResponse));
      break;
    default:
      break;
    }
    break;
  default:
    break;
  }
}

void_t ClientHandler::handleLogin(uint32_t requestId, BotProtocol::LoginRequest& loginRequest)
{
  String userName = BotProtocol::getString(loginRequest.userName);
  user = serverHandler.findUser(userName);
  if(!user)
  {
    sendErrorResponse(BotProtocol::loginRequest, requestId, 0, "Unknown user.");
    return;
  }

  for(uint32_t* p = (uint32_t*)loginkey, * end = (uint32_t*)(loginkey + 32); p < end; ++p)
    *p = Math::random();

  BotProtocol::LoginResponse loginResponse;
  Memory::copy(loginResponse.userKey, user->getKey(), sizeof(loginResponse.userKey));
  Memory::copy(loginResponse.loginKey, loginkey, sizeof(loginResponse.loginKey));
  sendMessage(BotProtocol::loginResponse, requestId, &loginResponse, sizeof(loginResponse));
  state = loginState;
}

void ClientHandler::handleAuth(uint32_t requestId, BotProtocol::AuthRequest& authRequest)
{
  byte_t signature[32];
  Sha256::hmac(loginkey, 32, user->getPwHmac(), 32, signature);
  if(Memory::compare(signature, authRequest.signature, 32) != 0)
  {
    sendErrorResponse(BotProtocol::authRequest, requestId, 0, "Incorrect signature.");
    return;
  }

  sendMessage(BotProtocol::authResponse, requestId, 0, 0);
  state = userState;
  user->registerClient(*this);

  // send engine list
  {
    BotProtocol::BotEngine botEngine;
    const HashMap<uint32_t, BotEngine*>& botEngines = serverHandler.getBotEngines();
    for(HashMap<uint32_t, BotEngine*>::Iterator i = botEngines.begin(), end = botEngines.end(); i != end; ++i)
    {
      (*i)->getEntity(botEngine);
      sendUpdateEntity(0, &botEngine, sizeof(botEngine));
    }
  }

  // send market adapter list
  {
    BotProtocol::MarketAdapter marketAdapter;
    const HashMap<uint32_t, MarketAdapter*>& marketAdapters = serverHandler.getMarketAdapters();
    for(HashMap<uint32_t, MarketAdapter*>::Iterator i = marketAdapters.begin(), end = marketAdapters.end(); i != end; ++i)
    {
      (*i)->getEntity(marketAdapter);
      sendUpdateEntity(0, &marketAdapter, sizeof(marketAdapter));
    }
  }

  // send market list
  {
    BotProtocol::Market market;
    const HashMap<uint32_t, Market*>& markets = user->getMarkets();
    for(HashMap<uint32_t, Market*>::Iterator i = markets.begin(), end = markets.end(); i != end; ++i)
    {
      (*i)->getEntity(market);
      sendUpdateEntity(0, &market, sizeof(market));
    }
  }

  // send session list
  {
    BotProtocol::Session session;
    const HashMap<uint32_t, Session*>& sessions = user->getSessions();
    for(HashMap<uint32_t, Session*>::Iterator i = sessions.begin(), end = sessions.end(); i != end; ++i)
    {
      (*i)->getEntity(session);
      sendUpdateEntity(0, &session, sizeof(session));
    }
  }
}

void_t ClientHandler::handleRegisterBot(uint32_t requestId, BotProtocol::RegisterBotRequest& registerBotRequest)
{
  Session* session = serverHandler.findSessionByPid(registerBotRequest.pid);
  if(!session)
  {
    sendErrorResponse(BotProtocol::registerBotRequest, requestId, 0, "Unknown session.");
    return;
  }
  if(!session->registerClient(*this, Session::entityType))
  {
    sendErrorResponse(BotProtocol::registerBotRequest, requestId, 0, "Invalid session.");
    return;
  } 

  BotProtocol::RegisterBotResponse response;
  response.sessionId = session->getId();
  response.marketId = session->getMarket()->getId();
  sendMessage(BotProtocol::registerBotResponse, requestId, &response, sizeof(response));

  this->session = session;
  state = botState;

  BotProtocol::Session sessionEntity;
  session->getEntity(sessionEntity);
  session->getUser().sendUpdateEntity(&sessionEntity, sizeof(sessionEntity));
}

void_t ClientHandler::handleRegisterBotHandler(uint32_t requestId, BotProtocol::RegisterBotHandlerRequest& registerBotHandlerRequest)
{
  Session* session = serverHandler.findSessionByPid(registerBotHandlerRequest.pid);
  if(!session)
  {
    sendErrorResponse(BotProtocol::registerBotRequest, requestId, 0, "Unknown session.");
    return;
  }
  if(!session->registerClient(*this, Session::handlerType)) // this changes the state of the session
  {
    sendErrorResponse(BotProtocol::registerBotRequest, requestId, 0, "Invalid session.");
    return;
  } 

  BotProtocol::RegisterBotHandlerResponse response;
  MarketAdapter* marketAdapater = session->getMarket()->getMarketAdapter();
  BotProtocol::setString(response.marketAdapterName, session->getMarket()->getMarketAdapter()->getName());
  BotProtocol::setString(response.currencyBase, marketAdapater->getCurrencyBase());
  BotProtocol::setString(response.currencyComm, marketAdapater->getCurrencyComm());
  response.simulation = session->isSimulation();
  sendMessage(BotProtocol::registerBotHandlerResponse, requestId, &response, sizeof(response));

  this->session = session;
  state = botHandlerState;

  // send updated session entity to all connected clients
  BotProtocol::Session sessionEntity;
  session->getEntity(sessionEntity);
  session->getUser().sendUpdateEntity(&sessionEntity, sizeof(sessionEntity));
}

void_t ClientHandler::handleRegisterMarket(uint32_t requestId, BotProtocol::RegisterMarketRequest& registerMarketRequest)
{
  Market* market = serverHandler.findMarketByPid(registerMarketRequest.pid);
  if(!market)
  {
    sendErrorResponse(BotProtocol::registerMarketRequest, requestId, 0, "Unknown market.");
    return;
  }

  if(!market->registerClient(*this, Market::entityType))
  {
    sendErrorResponse(BotProtocol::registerMarketRequest, requestId, 0, "Invalid market.");
    return;
  } 

  this->market = market;
  state = marketState;

  sendMessage(BotProtocol::registerMarketResponse, requestId, 0, 0);
}

void_t ClientHandler::handleRegisterMarketHandler(uint32_t requestId, BotProtocol::RegisterMarketHandlerRequest& registerMarketHandlerRequest)
{
  Market* market = serverHandler.findMarketByPid(registerMarketHandlerRequest.pid);
  if(!market)
  {
    sendErrorResponse(BotProtocol::registerMarketHandlerRequest, requestId, 0, "Unknown market.");
    return;
  }
  if(!market->registerClient(*this, Market::handlerType)) // this changes the state of the market
  {
    sendErrorResponse(BotProtocol::registerMarketHandlerRequest, requestId, 0, "Invalid market.");
    return;
  } 

  BotProtocol::RegisterMarketHandlerResponse response;
  BotProtocol::setString(response.userName, market->getUserName());
  BotProtocol::setString(response.key, market->getKey());
  BotProtocol::setString(response.secret, market->getSecret());
  sendMessage(BotProtocol::registerMarketHandlerResponse, requestId, &response, sizeof(response));

  this->market = market;
  state = marketHandlerState;

  // send updated market entity to all connected clients
  BotProtocol::Market marketEntity;
  market->getEntity(marketEntity);
  market->getUser().sendUpdateEntity(&marketEntity, sizeof(marketEntity));

  // request market balance
  BotProtocol::ControlMarket controlMarket;
  controlMarket.entityType = BotProtocol::market;
  controlMarket.entityId = market->getId();
  controlMarket.cmd = BotProtocol::ControlMarket::refreshBalance;
  sendMessage(BotProtocol::controlEntity, requestId, &controlMarket, sizeof(controlMarket));
}

void_t ClientHandler::handlePing(const byte_t* data, size_t size)
{
  BotProtocol::Header header;
  header.size = sizeof(header) + size;
  header.messageType = BotProtocol::pingResponse;
  client.reserve(header.size);
  client.send((const byte_t*)&header, sizeof(header));
  if(size > 0)
    client.send(data, size);
}

void_t ClientHandler::handleCreateEntity(uint32_t requestId, BotProtocol::Entity& entity, size_t size)
{
  switch(state)
  {
  case userState:
    switch((BotProtocol::EntityType)entity.entityType)
    {
    case BotProtocol::session:
      if(size >= sizeof(BotProtocol::Session))
        handleUserCreateSession(requestId, *(BotProtocol::Session*)&entity);
      break;
    case BotProtocol::market:
      if(size >= sizeof(BotProtocol::Market))
        handleUserCreateMarket(requestId, *(BotProtocol::Market*)&entity);
      break;
    case BotProtocol::marketOrder:
      if(size >= sizeof(BotProtocol::Order))
        handleUserCreateMarketOrder(requestId, *(BotProtocol::Order*)&entity);
      break;
    case BotProtocol::sessionItem:
      if(size >= sizeof(BotProtocol::SessionItem))
        handleUserCreateSessionItem(requestId, *(BotProtocol::SessionItem*)&entity);
      break;
    default:
      break;
    }
    break;
  case botState:
    switch((BotProtocol::EntityType)entity.entityType)
    {
    case BotProtocol::sessionTransaction:
      if(size >= sizeof(BotProtocol::Transaction))
        handleBotCreateSessionTransaction(requestId, *(BotProtocol::Transaction*)&entity);
      break;
    case BotProtocol::sessionItem:
      if(size >= sizeof(BotProtocol::SessionItem))
        handleBotCreateSessionItem(requestId, *(BotProtocol::SessionItem*)&entity);
      break;
    case BotProtocol::sessionProperty:
      if(size >= sizeof(BotProtocol::SessionProperty))
        handleBotCreateSessionProperty(requestId, *(BotProtocol::SessionProperty*)&entity);
      break;
    case BotProtocol::sessionOrder:
      if(size >= sizeof(BotProtocol::Order))
        handleBotCreateSessionOrder(requestId, *(BotProtocol::Order*)&entity);
      break;
    case BotProtocol::sessionMarker:
      if(size >= sizeof(BotProtocol::Marker))
        handleBotCreateSessionMarker(requestId, *(BotProtocol::Marker*)&entity);
      break;
    case BotProtocol::sessionLogMessage:
      if(size >= sizeof(BotProtocol::Order))
        handleBotCreateSessionLogMessage(requestId, *(BotProtocol::SessionLogMessage*)&entity);
      break;
    case BotProtocol::marketOrder:
      if(size >= sizeof(BotProtocol::Order))
        handleBotCreateMarketOrder(requestId, *(BotProtocol::Order*)&entity);
    default:
      break;
    }
    break;
  default:
    break;
  }
}

void_t ClientHandler::handleRemoveEntity(uint32_t requestId, const BotProtocol::Entity& entity)
{
  switch(state)
  {
  case userState:
    switch((BotProtocol::EntityType)entity.entityType)
    {
    case BotProtocol::session:
      handleUserRemoveSession(requestId, entity);
      break;
    case BotProtocol::market:
      handleUserRemoveMarket(requestId, entity);
      break;
    case BotProtocol::marketOrder:
      handleUserRemoveMarketOrder(requestId, entity);
      break;
    case BotProtocol::sessionItem:
      handleUserRemoveSessionItem(requestId, entity);
      break;
    default:
      break;
    }
    break;
  case botState:
    switch((BotProtocol::EntityType)entity.entityType)
    {
    case BotProtocol::sessionTransaction:
      handleBotRemoveSessionTransaction(requestId, entity);
      break;
    case BotProtocol::sessionItem:
      handleBotRemoveSessionItem(requestId, entity);
      break;
    case BotProtocol::sessionProperty:
      handleBotRemoveSessionProperty(requestId, entity);
      break;
    case BotProtocol::sessionOrder:
      handleBotRemoveSessionOrder(requestId, entity);
      break;
    case BotProtocol::marketOrder:
      handleBotRemoveMarketOrder(requestId, entity);
      break;
    default:
      break;
    }
    break;
  case marketState:
    switch((BotProtocol::EntityType)entity.entityType)
    {
    case BotProtocol::marketTransaction:
      handleMarketRemoveMarketTransaction(requestId, entity);
      break;
    case BotProtocol::marketOrder:
      handleMarketRemoveMarketOrder(requestId, entity);
      break;
    default:
      break;
    }
    break;
  default:
    break;
  }
}

void_t ClientHandler::handleControlEntity(uint32_t requestId, BotProtocol::Entity& entity, size_t size)
{
  switch(state)
  {
  case userState:
    switch((BotProtocol::EntityType)entity.entityType)
    {
    case BotProtocol::session:
      if(size >= sizeof(BotProtocol::ControlSession))
        handleUserControlSession(requestId, *(BotProtocol::ControlSession*)&entity);
      break;
    case BotProtocol::market:
      if(size >= sizeof(BotProtocol::ControlMarket))
        handleUserControlMarket(requestId, *(BotProtocol::ControlMarket*)&entity);
      break;
    default:
      break;
    }
    break;
  case botState:
    switch((BotProtocol::EntityType)entity.entityType)
    {
    case BotProtocol::session:
      if(size >= sizeof(BotProtocol::ControlSession))
        handleBotControlSession(requestId, *(BotProtocol::ControlSession*)&entity);
      break;
    case BotProtocol::market:
      if(size >= sizeof(BotProtocol::ControlMarket))
        handleBotControlMarket(requestId, *(BotProtocol::ControlMarket*)&entity);
      break;
    default:
      break;
    }
    break;
  default:
    break;
  }
}

void_t ClientHandler::handleUpdateEntity(uint32_t requestId, BotProtocol::Entity& entity, size_t size)
{
  switch(state)
  {
  case userState:
    switch((BotProtocol::EntityType)entity.entityType)
    {
    case BotProtocol::marketOrder:
      if(size >= sizeof(BotProtocol::Order))
        handleUserUpdateMarketOrder(requestId, *(BotProtocol::Order*)&entity);
      break;
    case BotProtocol::sessionItem:
      if(size >= sizeof(BotProtocol::SessionItem))
        handleUserUpdateSessionItem(requestId, *(BotProtocol::SessionItem*)&entity);
      break;
    case BotProtocol::sessionProperty:
      if(size >= sizeof(BotProtocol::SessionProperty))
        handleUserUpdateSessionProperty(requestId, *(BotProtocol::SessionProperty*)&entity);
      break;
    default:
      break;
    }
    break;
  case marketState:
    switch((BotProtocol::EntityType)entity.entityType)
    {
    case BotProtocol::marketTransaction:
      if(size >= sizeof(BotProtocol::Transaction))
        handleMarketUpdateMarketTransaction(requestId, *(BotProtocol::Transaction*)&entity);
      break;
    case BotProtocol::marketOrder:
      if(size >= sizeof(BotProtocol::Order))
        handleMarketUpdateMarketOrder(requestId, *(BotProtocol::Order*)&entity);
      break;
    case BotProtocol::marketBalance:
      if(size >= sizeof(BotProtocol::Balance))
        handleMarketUpdateMarketBalance(requestId, *(BotProtocol::Balance*)&entity);
      break;
    default:
      break;
    }
    break;
  case botState:
    switch((BotProtocol::EntityType)entity.entityType)
    {
    case BotProtocol::sessionTransaction:
      if(size >= sizeof(BotProtocol::Transaction))
        handleBotUpdateSessionTransaction(requestId, *(BotProtocol::Transaction*)&entity);
      break;
    case BotProtocol::sessionItem:
      if(size >= sizeof(BotProtocol::SessionItem))
        handleBotUpdateSessionItem(requestId, *(BotProtocol::SessionItem*)&entity);
      break;
    case BotProtocol::sessionProperty:
      if(size >= sizeof(BotProtocol::SessionProperty))
        handleBotUpdateSessionProperty(requestId, *(BotProtocol::SessionProperty*)&entity);
      break;
    case BotProtocol::sessionOrder:
      if(size >= sizeof(BotProtocol::Order))
        handleBotUpdateSessionOrder(requestId, *(BotProtocol::Order*)&entity);
      break;
    default:
      break;
    }
    break;
  default:
    break;
  }
}

void_t ClientHandler::handleResponse(BotProtocol::MessageType messageType, uint32_t requestId, const BotProtocol::Entity& response, size_t size)
{
  switch(state)
  {
  case marketHandlerState:
  case botHandlerState:
    if(requestId != 0)
    {
      uint32_t requesterRequestId;
      ClientHandler* requester;
      if(!serverHandler.findAndRemoveRequestId(requestId, requesterRequestId, requester))
        return;
      requester->sendMessage(messageType, requesterRequestId, &response, size);
    }
    break;
  default:
    break;
  }
}

void_t ClientHandler::handleUserCreateMarket(uint32_t requestId, BotProtocol::Market& createMarketArgs)
{
  MarketAdapter* marketAdapter = serverHandler.findMarketAdapter(createMarketArgs.marketAdapterId);
  if(!marketAdapter)
  {
    sendErrorResponse(BotProtocol::createEntity, requestId, &createMarketArgs, "Unknown market adapter.");
    return;
  }
  
  String username = BotProtocol::getString(createMarketArgs.userName);
  String key = BotProtocol::getString(createMarketArgs.key);
  String secret = BotProtocol::getString(createMarketArgs.secret);
  Market* market = user->createMarket(*marketAdapter, username, key, secret);
  if(!market)
  {
    sendErrorResponse(BotProtocol::createEntity, requestId, &createMarketArgs, "Could not create market.");
    return;
  }

  BotProtocol::Market response;
  market->getEntity(response);
  sendMessage(BotProtocol::createEntityResponse, requestId, &response, sizeof(response));

  user->sendUpdateEntity(&response, sizeof(response));
  user->saveData();

  market->start();
  market->getEntity(response);
  user->sendUpdateEntity(&response, sizeof(response));
}

void_t ClientHandler::handleUserRemoveMarket(uint32_t requestId, const BotProtocol::Entity& entity)
{
  // todo: do not remove markts that are in use by a bot session

  if(!user->deleteMarket(entity.entityId))
  {
    sendErrorResponse(BotProtocol::removeEntity, requestId, &entity, "Unknown market.");
    return;
  }

  sendMessage(BotProtocol::removeEntityResponse, requestId, &entity, sizeof(entity));

  user->sendRemoveEntity(BotProtocol::market, entity.entityId);
  user->saveData();
}

void_t ClientHandler::handleUserControlMarket(uint32_t requestId, BotProtocol::ControlMarket& controlMarket)
{
  BotProtocol::ControlMarketResponse response;
  response.entityType = BotProtocol::market;
  response.entityId = controlMarket.entityId;
  response.cmd = controlMarket.cmd;

  Market* market = user->findMarket(controlMarket.entityId);
  if(!market)
  {
    sendErrorResponse(BotProtocol::controlEntity, requestId, &controlMarket, "Unknown market.");
    return;
  }

  switch((BotProtocol::ControlMarket::Command)controlMarket.cmd)
  {
  case BotProtocol::ControlMarket::select:
    if(this->market)
      this->market->unregisterClient(*this);
    market->registerClient(*this, Market::userType);
    this->market = market;
    sendMessage(BotProtocol::controlEntityResponse, requestId, &response, sizeof(response));
    sendRemoveAllEntities(BotProtocol::marketBalance);
    sendRemoveAllEntities(BotProtocol::marketOrder);
    sendRemoveAllEntities(BotProtocol::marketTransaction);
    {
      const BotProtocol::Balance& balance = market->getBalance();
      if(balance.entityType == BotProtocol::marketBalance)
        sendUpdateEntity(0, &balance, sizeof(balance));
      const HashMap<uint32_t, BotProtocol::Transaction>& transactions = market->getTransactions();
      for(HashMap<uint32_t, BotProtocol::Transaction>::Iterator i = transactions.begin(), end = transactions.end(); i != end; ++i)
        sendUpdateEntity(0, &*i, sizeof(BotProtocol::Transaction));
      const HashMap<uint32_t, BotProtocol::Order>& orders = market->getOrders();
      for(HashMap<uint32_t, BotProtocol::Order>::Iterator i = orders.begin(), end = orders.end(); i != end; ++i)
        sendUpdateEntity(0, &*i, sizeof(BotProtocol::Order));
    }
    break;
  case BotProtocol::ControlMarket::refreshTransactions:
  case BotProtocol::ControlMarket::refreshOrders:
  case BotProtocol::ControlMarket::refreshBalance:
    {
      ClientHandler* handlerClient = market->getHandlerClient();
      if(!handlerClient)
      {
        sendErrorResponse(BotProtocol::controlEntity, requestId, &controlMarket, "No market handler.");
        return;
      }
      uint32_t requesteeRequestId = serverHandler.createRequestId(requestId, *this, *handlerClient);
      handlerClient->sendMessage(BotProtocol::controlEntity, requesteeRequestId, &controlMarket, sizeof(controlMarket));
    }
    break;
  default:
    break;
  }
}

void_t ClientHandler::handleUserCreateSession(uint32_t requestId, BotProtocol::Session& createSessionArgs)
{
  String name = BotProtocol::getString(createSessionArgs.name);
  BotEngine* botEngine = serverHandler.findBotEngine(createSessionArgs.botEngineId);
  if(!botEngine)
  {
    sendErrorResponse(BotProtocol::createEntity, requestId, &createSessionArgs, "Unknown bot engine.");
    return;
  }

  Market* market = user->findMarket(createSessionArgs.marketId);
  if(!market)
  {
    sendErrorResponse(BotProtocol::createEntity, requestId, &createSessionArgs, "Unknown market.");
    return;
  }

  Session* session = user->createSession(name, *botEngine, *market);
  if(!session)
  {
    sendErrorResponse(BotProtocol::createEntity, requestId, &createSessionArgs, "Could not create session.");
    return;
  }

  BotProtocol::Session response;
  session->getEntity(response);
  sendMessage(BotProtocol::createEntityResponse, requestId, &response, sizeof(response));

  user->sendUpdateEntity(&response, sizeof(response));
  user->saveData();
}

void_t ClientHandler::handleUserRemoveSession(uint32_t requestId, const BotProtocol::Entity& entity)
{
  if(!user->deleteSession(entity.entityId))
  {
    sendErrorResponse(BotProtocol::removeEntity, requestId, &entity, "Unknown session.");
    return;
  }

  sendMessage(BotProtocol::removeEntityResponse, requestId, &entity, sizeof(entity));

  user->sendRemoveEntity(BotProtocol::session, entity.entityId);
  user->saveData();
}

void_t ClientHandler::handleUserControlSession(uint32_t requestId, BotProtocol::ControlSession& controlSession)
{
  BotProtocol::ControlSessionResponse response;
  response.entityType = BotProtocol::session;
  response.entityId = controlSession.entityId;
  response.cmd = controlSession.cmd;

  Session* session = user->findSession(controlSession.entityId);
  if(!session)
  {
    sendErrorResponse(BotProtocol::controlEntity, requestId, &controlSession, "Unknown session.");
    return;
  }

  switch((BotProtocol::ControlSession::Command)controlSession.cmd)
  {
  case BotProtocol::ControlSession::startSimulation:
    if(!session->startSimulation())
    {
      sendErrorResponse(BotProtocol::controlEntity, requestId, &controlSession, "Could not start simulation session.");
      return;
    }
    sendMessage(BotProtocol::controlEntityResponse, requestId, &response, sizeof(response));
    {
      BotProtocol::Session sessionEntity;
      session->getEntity(sessionEntity);
      session->getUser().sendUpdateEntity(&sessionEntity, sizeof(sessionEntity));
    }
    // todo: update balance?
    session->sendRemoveAllEntities(BotProtocol::sessionTransaction);
    //session->sendRemoveAllEntities(BotProtocol::sessionItem);
    session->sendRemoveAllEntities(BotProtocol::sessionOrder);
    session->sendRemoveAllEntities(BotProtocol::sessionLogMessage);
    session->sendRemoveAllEntities(BotProtocol::sessionMarker);
    break;
  case BotProtocol::ControlSession::startLive:
    if(!session->startLive())
    {
      sendErrorResponse(BotProtocol::controlEntity, requestId, &controlSession, "Could not start simulation session.");
      return;
    }
    sendMessage(BotProtocol::controlEntityResponse, requestId, &response, sizeof(response));
    {
      BotProtocol::Session sessionEntity;
      session->getEntity(sessionEntity);
      session->getUser().sendUpdateEntity(&sessionEntity, sizeof(sessionEntity));
    }
    break;
  case BotProtocol::ControlSession::stop:
    if(!session->stop())
    {
      sendErrorResponse(BotProtocol::controlEntity, requestId, &controlSession, "Could not stop session.");
      return;
    }
    sendMessage(BotProtocol::controlEntityResponse, requestId, &response, sizeof(response));
    {
      BotProtocol::Session sessionEntity;
      session->getEntity(sessionEntity);
      session->getUser().sendUpdateEntity(&sessionEntity, sizeof(sessionEntity));
    }
    // todo: do this only if a simulation (or optimization?) was stopped
    // todo: update balance?
    session->sendRemoveAllEntities(BotProtocol::sessionTransaction);
    session->sendRemoveAllEntities(BotProtocol::sessionItem);
    session->sendRemoveAllEntities(BotProtocol::sessionProperty);
    session->sendRemoveAllEntities(BotProtocol::sessionOrder);
    session->sendRemoveAllEntities(BotProtocol::sessionLogMessage);
    session->sendRemoveAllEntities(BotProtocol::sessionMarker);
    {
      const HashMap<uint32_t, BotProtocol::Transaction>& transactions = session->getTransactions();
      for(HashMap<uint32_t, BotProtocol::Transaction>::Iterator i = transactions.begin(), end = transactions.end(); i != end; ++i)
        sendUpdateEntity(0, &*i, sizeof(BotProtocol::Transaction));
      const HashMap<uint32_t, BotProtocol::SessionItem>& items = session->getItems();
      for(HashMap<uint32_t, BotProtocol::SessionItem>::Iterator i = items.begin(), end = items.end(); i != end; ++i)
        sendUpdateEntity(0, &*i, sizeof(BotProtocol::SessionItem));
      const HashMap<uint32_t, BotProtocol::SessionProperty>& properties = session->getProperties();
      for(HashMap<uint32_t, BotProtocol::SessionProperty>::Iterator i = properties.begin(), end = properties.end(); i != end; ++i)
        sendUpdateEntity(0, &*i, sizeof(BotProtocol::SessionProperty));
      const HashMap<uint32_t, BotProtocol::Order>& orders = session->getOrders();
      for(HashMap<uint32_t, BotProtocol::Order>::Iterator i = orders.begin(), end = orders.end(); i != end; ++i)
        sendUpdateEntity(0, &*i, sizeof(BotProtocol::Order));
      const HashMap<uint32_t, BotProtocol::Marker>& markers = session->getMarkers();
      for(HashMap<uint32_t, BotProtocol::Marker>::Iterator i = markers.begin(), end = markers.end(); i != end; ++i)
        sendUpdateEntity(0, &*i, sizeof(BotProtocol::Marker));
      const List<BotProtocol::SessionLogMessage>& logMessages = session->getLogMessages();
      for(List<BotProtocol::SessionLogMessage>::Iterator i = logMessages.begin(), end = logMessages.end(); i != end; ++i)
        sendUpdateEntity(0, &*i, sizeof(BotProtocol::SessionLogMessage));
    }
    break;
  case BotProtocol::ControlSession::select:
    if(this->session)
      this->session->unregisterClient(*this);
    session->registerClient(*this, Session::userType);
    this->session = session;
    sendMessage(BotProtocol::controlEntityResponse, requestId, &response, sizeof(response));
    sendRemoveAllEntities(BotProtocol::sessionTransaction);
    sendRemoveAllEntities(BotProtocol::sessionItem);
    sendRemoveAllEntities(BotProtocol::sessionProperty);
    sendRemoveAllEntities(BotProtocol::sessionOrder);
    sendRemoveAllEntities(BotProtocol::sessionLogMessage);
    sendRemoveAllEntities(BotProtocol::sessionMarker);
    {
      const HashMap<uint32_t, BotProtocol::Transaction>& transactions = session->getTransactions();
      for(HashMap<uint32_t, BotProtocol::Transaction>::Iterator i = transactions.begin(), end = transactions.end(); i != end; ++i)
        sendUpdateEntity(0, &*i, sizeof(BotProtocol::Transaction));
      const HashMap<uint32_t, BotProtocol::SessionItem>& items = session->getItems();
      for(HashMap<uint32_t, BotProtocol::SessionItem>::Iterator i = items.begin(), end = items.end(); i != end; ++i)
        sendUpdateEntity(0, &*i, sizeof(BotProtocol::SessionItem));
      const HashMap<uint32_t, BotProtocol::SessionProperty>& properties = session->getProperties();
      for(HashMap<uint32_t, BotProtocol::SessionProperty>::Iterator i = properties.begin(), end = properties.end(); i != end; ++i)
        sendUpdateEntity(0, &*i, sizeof(BotProtocol::SessionProperty));
      const HashMap<uint32_t, BotProtocol::Order>& orders = session->getOrders();
      for(HashMap<uint32_t, BotProtocol::Order>::Iterator i = orders.begin(), end = orders.end(); i != end; ++i)
        sendUpdateEntity(0, &*i, sizeof(BotProtocol::Order));
      const HashMap<uint32_t, BotProtocol::Marker>& markers = session->getMarkers();
      for(HashMap<uint32_t, BotProtocol::Marker>::Iterator i = markers.begin(), end = markers.end(); i != end; ++i)
        sendUpdateEntity(0, &*i, sizeof(BotProtocol::Marker));
      const List<BotProtocol::SessionLogMessage>& logMessages = session->getLogMessages();
      for(List<BotProtocol::SessionLogMessage>::Iterator i = logMessages.begin(), end = logMessages.end(); i != end; ++i)
        sendUpdateEntity(0, &*i, sizeof(BotProtocol::SessionLogMessage));
    }
    break;
  default:
    break;
  }
}

void_t ClientHandler::handleBotControlSession(uint32_t requestId, BotProtocol::ControlSession& controlSession)
{
  BotProtocol::ControlSessionResponse response;
  response.entityType = BotProtocol::session;
  response.entityId = controlSession.entityId;
  response.cmd = controlSession.cmd;

  if(controlSession.entityId != session->getId())
  {
    sendErrorResponse(BotProtocol::controlEntity, requestId, &controlSession, "Unknown session.");
    return;
  }

  switch((BotProtocol::ControlSession::Command)controlSession.cmd)
  {
  case BotProtocol::ControlSession::requestTransactions:
    {
      const HashMap<uint32_t, BotProtocol::Transaction>& transactions = session->getTransactions();
      size_t dataSize = sizeof(response) + transactions.size() * sizeof(BotProtocol::Transaction);
      sendMessageHeader(BotProtocol::controlEntityResponse, requestId, dataSize);
      sendMessageData(&response, sizeof(response));
      for(HashMap<uint32_t, BotProtocol::Transaction>::Iterator i = transactions.begin(), end = transactions.end(); i != end; ++i)
        sendMessageData(&*i, sizeof(BotProtocol::Transaction));
    }
    break;
  case BotProtocol::ControlSession::requestItems:
    {
      const HashMap<uint32_t, BotProtocol::SessionItem>& items = session->getItems();
      size_t dataSize = sizeof(response) + items.size() * sizeof(BotProtocol::SessionItem);
      sendMessageHeader(BotProtocol::controlEntityResponse, requestId, dataSize);
      sendMessageData(&response, sizeof(response));
      for(HashMap<uint32_t, BotProtocol::SessionItem>::Iterator i = items.begin(), end = items.end(); i != end; ++i)
        sendMessageData(&*i, sizeof(BotProtocol::SessionItem));
    }
    break;
  case BotProtocol::ControlSession::requestProperties:
    {
      const HashMap<uint32_t, BotProtocol::SessionProperty>& properties = session->getProperties();
      size_t dataSize = sizeof(response) + properties.size() * sizeof(BotProtocol::SessionProperty);
      sendMessageHeader(BotProtocol::controlEntityResponse, requestId, dataSize);
      sendMessageData(&response, sizeof(response));
      for(HashMap<uint32_t, BotProtocol::SessionProperty>::Iterator i = properties.begin(), end = properties.end(); i != end; ++i)
        sendMessageData(&*i, sizeof(BotProtocol::SessionProperty));
    }
    break;
  case BotProtocol::ControlSession::requestOrders:
    {
      const HashMap<uint32_t, BotProtocol::Order>& orders = session->getOrders();
      size_t dataSize = sizeof(response) + orders.size() * sizeof(BotProtocol::Order);
      sendMessageHeader(BotProtocol::controlEntityResponse, requestId, dataSize);
      sendMessageData(&response, sizeof(response));
      for(HashMap<uint32_t, BotProtocol::Order>::Iterator i = orders.begin(), end = orders.end(); i != end; ++i)
        sendMessageData(&*i, sizeof(BotProtocol::Order));
    }
    break;
  default:
    break;
  }
}

void_t ClientHandler::handleBotControlMarket(uint32_t requestId, BotProtocol::ControlMarket& controlMarket)
{
  Market* market = session->getMarket();
  if(controlMarket.entityId != market->getId())
  {
    sendErrorResponse(BotProtocol::controlEntity, requestId, &controlMarket, "Unknown market.");
    return;
  }

  switch((BotProtocol::ControlMarket::Command)controlMarket.cmd)
  {
  case BotProtocol::ControlMarket::requestTransactions:
  case BotProtocol::ControlMarket::requestOrders:
  case BotProtocol::ControlMarket::requestBalance:
    {
      ClientHandler* handlerClient = market->getHandlerClient();
      if(!handlerClient)
      {
        sendErrorResponse(BotProtocol::controlEntity, requestId, &controlMarket, "No market handler.");
        return;
      }
      uint32_t requesteeRequestId = serverHandler.createRequestId(requestId, *this, *handlerClient);
      handlerClient->sendMessage(BotProtocol::controlEntity, requesteeRequestId, &controlMarket, sizeof(controlMarket));
    }
    break;
//  case BotProtocol::ControlMarket::requestMarketAdapter:
//    {
//      BotProtocol::ControlSessionResponse response;
//      response.entityType = BotProtocol::market;
//      response.entityId = controlMarket.entityId;
//      response.cmd = controlMarket.cmd;
//      BotProtocol::MarketAdapter marketAdapter;
//      market->getMarketAdapter()->getEntity(marketAdapter);
//      size_t dataSize = sizeof(response) + sizeof(BotProtocol::MarketAdapter);
//      sendMessageHeader(BotProtocol::controlEntityResponse, requestId, dataSize);
//      sendMessageData(&response, sizeof(response));
//      sendMessageData(&marketAdapter, sizeof(BotProtocol::MarketAdapter));
//    }
//    break;
  default:
    break;
  }
}

void_t ClientHandler::handleBotCreateSessionTransaction(uint32_t requestId, BotProtocol::Transaction& createTransactionArgs)
{
  BotProtocol::Transaction& transaction = session->createTransaction(createTransactionArgs);

  sendMessage(BotProtocol::createEntityResponse, requestId, &transaction, sizeof(transaction));

  session->sendUpdateEntity(&transaction, sizeof(transaction));
  session->saveData();
}

void_t ClientHandler::handleBotUpdateSessionTransaction(uint32_t requestId, BotProtocol::Transaction& transaction)
{
  if(!session->updateTransaction(transaction))
  {
    sendErrorResponse(BotProtocol::updateEntity, requestId, &transaction, "Could not update session transaction.");
    return;
  }

  sendMessage(BotProtocol::updateEntityResponse, requestId, &transaction, sizeof(BotProtocol::Entity));

  session->sendUpdateEntity(&transaction, sizeof(transaction));
  session->saveData();
}

void_t ClientHandler::handleBotRemoveSessionTransaction(uint32_t requestId, const BotProtocol::Entity& entity)
{
  if(!session->deleteTransaction(entity.entityId))
  {
    sendErrorResponse(BotProtocol::removeEntity, requestId, &entity, "Unknown session transaction.");
    return;
  }

  sendMessage(BotProtocol::removeEntityResponse, requestId, &entity, sizeof(entity));

  session->sendRemoveEntity(BotProtocol::sessionTransaction, entity.entityId);
  session->saveData();
}

void_t ClientHandler::handleBotCreateSessionItem(uint32_t requestId, BotProtocol::SessionItem& args)
{
  BotProtocol::SessionItem& item = session->createItem(args);

  sendMessage(BotProtocol::createEntityResponse, requestId, &item, sizeof(item));

  session->sendUpdateEntity(&item, sizeof(item));
  session->saveData();
}

void_t ClientHandler::handleBotUpdateSessionItem(uint32_t requestId, BotProtocol::SessionItem& item)
{
  if(!session->updateItem(item))
  {
    sendErrorResponse(BotProtocol::updateEntity, requestId, &item, "Could not update session item.");
    return;
  }

  sendMessage(BotProtocol::updateEntityResponse, requestId, &item, sizeof(BotProtocol::Entity));

  session->sendUpdateEntity(&item, sizeof(item));
  session->saveData();
}

void_t ClientHandler::handleBotRemoveSessionItem(uint32_t requestId, const BotProtocol::Entity& entity)
{
  if(!session->deleteItem(entity.entityId))
  {
    sendErrorResponse(BotProtocol::removeEntity, requestId, &entity, "Unknown session item.");
    return;
  }

  sendMessage(BotProtocol::removeEntityResponse, requestId, &entity, sizeof(entity));

  session->sendRemoveEntity(BotProtocol::sessionItem, entity.entityId);
  session->saveData();
}

void_t ClientHandler::handleBotCreateSessionProperty(uint32_t requestId, BotProtocol::SessionProperty& args)
{
  BotProtocol::SessionProperty& property = session->createProperty(args);

  sendMessage(BotProtocol::createEntityResponse, requestId, &property, sizeof(property));

  session->sendUpdateEntity(&property, sizeof(property));
  session->saveData();
}

void_t ClientHandler::handleBotUpdateSessionProperty(uint32_t requestId, BotProtocol::SessionProperty& property)
{
  if(!session->updateProperty(property))
  {
    sendErrorResponse(BotProtocol::updateEntity, requestId, &property, "Could not update session property.");
    return;
  }

  sendMessage(BotProtocol::updateEntityResponse, requestId, &property, sizeof(BotProtocol::Entity));

  session->sendUpdateEntity(&property, sizeof(property));
  session->saveData();
}

void_t ClientHandler::handleBotRemoveSessionProperty(uint32_t requestId, const BotProtocol::Entity& entity)
{
  if(!session->deleteProperty(entity.entityId))
  {
    sendErrorResponse(BotProtocol::removeEntity, requestId, &entity, "Unknown session property.");
    return;
  }

  sendMessage(BotProtocol::removeEntityResponse, requestId, &entity, sizeof(entity));

  session->sendRemoveEntity(BotProtocol::sessionProperty, entity.entityId);
  session->saveData();
}

void_t ClientHandler::handleBotCreateSessionOrder(uint32_t requestId, BotProtocol::Order& createOrderArgs)
{
  BotProtocol::Order& order = session->createOrder(createOrderArgs);

  sendMessage(BotProtocol::createEntityResponse, requestId, &order, sizeof(order));

  session->sendUpdateEntity(&order, sizeof(order));
  session->saveData();
}

void_t ClientHandler::handleBotUpdateSessionOrder(uint32_t requestId, BotProtocol::Order& order)
{
  session->updateOrder(order);

  sendMessage(BotProtocol::updateEntityResponse, requestId, &order, sizeof(BotProtocol::Entity));

  session->sendUpdateEntity(&order, sizeof(order));
  session->saveData();
}

void_t ClientHandler::handleBotRemoveSessionOrder(uint32_t requestId, const BotProtocol::Entity& entity)
{
  if(!session->deleteOrder(entity.entityId))
  {
    sendErrorResponse(BotProtocol::removeEntity, requestId, &entity, "Unknown session order.");
    return;
  }

  sendMessage(BotProtocol::removeEntityResponse, requestId, &entity, sizeof(entity));

  session->sendRemoveEntity(BotProtocol::sessionOrder, entity.entityId);
  session->saveData();
}

void_t ClientHandler::handleBotCreateSessionMarker(uint32_t requestId, BotProtocol::Marker& markerArgs)
{
  BotProtocol::Marker& marker = session->createMarker(markerArgs);

  sendMessage(BotProtocol::createEntityResponse, requestId, &marker, sizeof(marker));

  session->sendUpdateEntity(&marker, sizeof(marker));
  session->saveData();
}

void_t ClientHandler::handleBotCreateSessionLogMessage(uint32_t requestId, BotProtocol::SessionLogMessage& logMessageArgs)
{
  BotProtocol::SessionLogMessage& logMessage = session->addLogMessage(logMessageArgs);

  sendMessage(BotProtocol::createEntityResponse, requestId, &logMessage, sizeof(logMessage));

  session->sendUpdateEntity(&logMessage, sizeof(logMessage));
  session->saveData();
}

void_t ClientHandler::handleBotCreateMarketOrder(uint32_t requestId, BotProtocol::Order& createOrderArgs)
{
  Market* market = session->getMarket();
  ClientHandler* handlerClient = market->getHandlerClient();
  if(!handlerClient)
  {
    sendErrorResponse(BotProtocol::createEntity, requestId, &createOrderArgs, "No market handler.");
    return;
  }

  uint32_t requesteeRequestId = serverHandler.createRequestId(requestId, *this, *handlerClient);
  handlerClient->sendMessage(BotProtocol::createEntity, requesteeRequestId, &createOrderArgs, sizeof(createOrderArgs));
}

void_t ClientHandler::handleBotRemoveMarketOrder(uint32_t requestId, const BotProtocol::Entity& entity)
{
  Market* market = session->getMarket();
  ClientHandler* handlerClient = market->getHandlerClient();
  if(!handlerClient)
  {
    sendErrorResponse(BotProtocol::removeEntity, requestId, &entity, "No market handler.");
    return;
  }

  uint32_t requesteeRequestId = serverHandler.createRequestId(requestId, *this, *handlerClient);
  handlerClient->sendRemoveEntity(requesteeRequestId, BotProtocol::marketOrder, entity.entityId);
}

void_t ClientHandler::handleMarketUpdateMarketTransaction(uint32_t requestId, BotProtocol::Transaction& transaction)
{
  if(!market->updateTransaction(transaction))
  {
    sendErrorResponse(BotProtocol::updateEntity, requestId, &transaction, "Could not update market transaction.");
    return;
  }

  sendMessage(BotProtocol::updateEntityResponse, requestId, &transaction, sizeof(BotProtocol::Entity));

  market->sendUpdateEntity(&transaction, sizeof(transaction));
}

void_t ClientHandler::handleMarketRemoveMarketTransaction(uint32_t requestId, const BotProtocol::Entity& entity)
{
  if(!market->deleteTransaction(entity.entityId))
  {
    sendErrorResponse(BotProtocol::removeEntity, requestId, &entity, "Unknown market transaction.");
    return;
  }

  sendMessage(BotProtocol::removeEntityResponse, requestId, &entity, sizeof(entity));

  market->sendRemoveEntity(BotProtocol::marketTransaction, entity.entityId);
}

void_t ClientHandler::handleMarketUpdateMarketOrder(uint32_t requestId, BotProtocol::Order& order)
{
  if(!market->updateOrder(order))
  {
    sendErrorResponse(BotProtocol::updateEntity, requestId, &order, "Could not update market order.");
    return;
  }

  sendMessage(BotProtocol::updateEntityResponse, requestId, &order, sizeof(BotProtocol::Entity));

  market->sendUpdateEntity(&order, sizeof(order));
}

void_t ClientHandler::handleMarketRemoveMarketOrder(uint32_t requestId, const BotProtocol::Entity& entity)
{
  if(!market->deleteOrder(entity.entityId))
  {
    sendErrorResponse(BotProtocol::removeEntity, requestId, &entity, "Unknown market order.");
    return;
  }

  sendMessage(BotProtocol::removeEntityResponse, requestId, &entity, sizeof(entity));

  market->sendRemoveEntity(BotProtocol::marketOrder, entity.entityId);
}

void_t ClientHandler::handleMarketUpdateMarketBalance(uint32_t requestId, BotProtocol::Balance& balance)
{
  if(!market->updateBalance(balance))
  {
    sendErrorResponse(BotProtocol::updateEntity, requestId, &balance, "Could not update market balance.");
    return;
  }

  sendMessage(BotProtocol::updateEntityResponse, requestId, &balance, sizeof(BotProtocol::Entity));

  market->sendUpdateEntity(&balance, sizeof(balance));
}

void_t ClientHandler::handleUserCreateMarketOrder(uint32_t requestId, BotProtocol::Order& createOrderArgs)
{
  if(!market)
  {
    sendErrorResponse(BotProtocol::createEntity, requestId, &createOrderArgs, "Invalid market.");
    return;
  }
  ClientHandler* handlerClient = market->getHandlerClient();
  if(!handlerClient)
  {
    sendErrorResponse(BotProtocol::createEntity, requestId, &createOrderArgs, "No market handler.");
    return;
  }

  uint32_t requesteeRequestId = serverHandler.createRequestId(requestId, *this, *handlerClient);
  handlerClient->sendMessage(BotProtocol::createEntity, requesteeRequestId, &createOrderArgs, sizeof(createOrderArgs));
}

void_t ClientHandler::handleUserUpdateMarketOrder(uint32_t requestId, BotProtocol::Order& order)
{
  if(!market)
  {
    sendErrorResponse(BotProtocol::updateEntity, requestId, &order, "Invalid market.");
    return;
  }
  ClientHandler* handlerClient = market->getHandlerClient();
  if(!handlerClient)
  {
    sendErrorResponse(BotProtocol::updateEntity, requestId, &order, "No market handler.");
    return;
  }

  uint32_t requesteeRequestId = serverHandler.createRequestId(requestId, *this, *handlerClient);
  handlerClient->sendMessage(BotProtocol::updateEntity, requesteeRequestId, &order, sizeof(order));
}

void_t ClientHandler::handleUserRemoveMarketOrder(uint32_t requestId, const BotProtocol::Entity& entity)
{
  if(!market)
  {
    sendErrorResponse(BotProtocol::removeEntity, requestId, &entity, "Invalid market.");
    return;
  }
  ClientHandler* handlerClient = market->getHandlerClient();
  if(!handlerClient)
  {
    sendErrorResponse(BotProtocol::removeEntity, requestId, &entity, "No market handler.");
    return;
  }

  uint32_t requesteeRequestId = serverHandler.createRequestId(requestId, *this, *handlerClient);
  handlerClient->sendRemoveEntity(requesteeRequestId, BotProtocol::marketOrder, entity.entityId);
}

void_t ClientHandler::handleUserCreateSessionItem(uint32_t requestId, BotProtocol::SessionItem& sessionItemArgs)
{
  if(!session)
  {
    sendErrorResponse(BotProtocol::createEntity, requestId, &sessionItemArgs, "Invalid session.");
    return;
  }

  sessionItemArgs.state = sessionItemArgs.type == BotProtocol::SessionItem::buy ? BotProtocol::SessionItem::waitBuy : BotProtocol::SessionItem::waitSell;
  sessionItemArgs.price = 0.;
  sessionItemArgs.profitablePrice = 0.;
  sessionItemArgs.orderId = 0;

  ClientHandler* handlerClient = session->getHandlerClient();
  if(handlerClient)
  {
    uint32_t requesteeRequestId = serverHandler.createRequestId(requestId, *this, *handlerClient);
    handlerClient->sendMessage(BotProtocol::createEntity, requesteeRequestId, &sessionItemArgs, sizeof(sessionItemArgs));
  }
  else
  {

    BotProtocol::SessionItem& item = session->createItem(sessionItemArgs);

    sendMessage(BotProtocol::createEntityResponse, requestId, &item, sizeof(item));

    session->sendUpdateEntity(&item, sizeof(item));
    session->saveData();
  }
}

void_t ClientHandler::handleUserUpdateSessionItem(uint32_t requestId, BotProtocol::SessionItem& sessionItem)
{
  if(!session)
  {
    sendErrorResponse(BotProtocol::updateEntity, requestId, &sessionItem, "Invalid session.");
    return;
  }

  const BotProtocol::SessionItem* item = session->getItem(sessionItem.entityId);
  if(!item)
  {
    sendErrorResponse(BotProtocol::removeEntity, requestId, &sessionItem, "Could not find session item.");
    return;
  }

  BotProtocol::SessionItem updatedItem = *item;
  updatedItem.flipPrice = sessionItem.flipPrice;

  ClientHandler* handlerClient = session->getHandlerClient();
  if(handlerClient)
  {
    uint32_t requesteeRequestId = serverHandler.createRequestId(requestId, *this, *handlerClient);
    handlerClient->sendMessage(BotProtocol::updateEntity, requesteeRequestId, &updatedItem, sizeof(updatedItem));
  }
  else
  {
    session->updateItem(updatedItem);

    sendMessage(BotProtocol::updateEntityResponse, requestId, &updatedItem, sizeof(BotProtocol::Entity));

    session->sendUpdateEntity(&updatedItem, sizeof(updatedItem));
    session->saveData();
  }
}

void_t ClientHandler::handleUserRemoveSessionItem(uint32_t requestId, const BotProtocol::Entity& entity)
{
  if(!session)
  {
    sendErrorResponse(BotProtocol::removeEntity, requestId, &entity, "Invalid session.");
    return;
  }
  ClientHandler* handlerClient = session->getHandlerClient();
  if(handlerClient)
  {
    uint32_t requesteeRequestId = serverHandler.createRequestId(requestId, *this, *handlerClient);
    handlerClient->sendRemoveEntity(requesteeRequestId, BotProtocol::sessionItem, entity.entityId);
  }
  else
  {
    if(!session->deleteItem(entity.entityId))
    {
      sendErrorResponse(BotProtocol::removeEntity, requestId, &entity, "Unknown session item.");
      return;
    }

    sendMessage(BotProtocol::removeEntityResponse, requestId, &entity, sizeof(entity));

    session->sendRemoveEntity(BotProtocol::sessionItem, entity.entityId);
    session->saveData();
  }
}

void_t ClientHandler::handleUserUpdateSessionProperty(uint32_t requestId, BotProtocol::SessionProperty& sessionProperty)
{
  if(!session)
  {
    sendErrorResponse(BotProtocol::updateEntity, requestId, &sessionProperty, "Invalid session.");
    return;
  }

  const BotProtocol::SessionProperty* property = session->getProperty(sessionProperty.entityId);
  if(!property)
  {
    sendErrorResponse(BotProtocol::removeEntity, requestId, &sessionProperty, "Could not find session property.");
    return;
  }

  if(property->flags & BotProtocol::SessionProperty::readOnly)
  {
    sendErrorResponse(BotProtocol::removeEntity, requestId, &sessionProperty, "Property is not editable.");
    return;
  }

  BotProtocol::SessionProperty updatedProperty = *property;
  BotProtocol::setString(updatedProperty.value, BotProtocol::getString(sessionProperty.value));

  ClientHandler* handlerClient = session->getHandlerClient();
  if(handlerClient)
  {
    uint32_t requesteeRequestId = serverHandler.createRequestId(requestId, *this, *handlerClient);
    handlerClient->sendMessage(BotProtocol::updateEntity, requesteeRequestId, &updatedProperty, sizeof(updatedProperty));
  }
  else
  {
    session->updateProperty(updatedProperty);

    sendMessage(BotProtocol::updateEntityResponse, requestId, &updatedProperty, sizeof(BotProtocol::Entity));

    session->sendUpdateEntity(&updatedProperty, sizeof(updatedProperty));
    session->saveData();
  }
}

void_t ClientHandler::sendMessage(BotProtocol::MessageType type, uint32_t requestId, const void_t* data, size_t size)
{
  BotProtocol::Header header;
  header.size = sizeof(header) + size;
  header.messageType = type;
  header.requestId = requestId;
  client.reserve(header.size);
  client.send((const byte_t*)&header, sizeof(header));
  if(size > 0)
    client.send((const byte_t*)data, size);
}

void_t ClientHandler::sendMessageHeader(BotProtocol::MessageType type, uint32_t requestId, size_t dataSize)
{
  BotProtocol::Header header;
  header.size = sizeof(header) + dataSize;
  header.messageType = type;
  header.requestId = requestId;
  client.reserve(header.size);
  client.send((const byte_t*)&header, sizeof(header));
}

void_t ClientHandler::sendMessageData(const void_t* data, size_t size)
{
  client.send((const byte_t*)data, size);
}

void_t ClientHandler::sendUpdateEntity(uint32_t requestId, const void_t* data, size_t size)
{
  ASSERT(size >= sizeof(BotProtocol::Entity));
  BotProtocol::Header header;
  header.size = sizeof(header) + size;
  header.messageType = BotProtocol::updateEntity;
  header.requestId = requestId;
  client.reserve(header.size);
  client.send((const byte_t*)&header, sizeof(header));
  client.send((const byte_t*)data, size);
}

void_t ClientHandler::sendRemoveEntity(uint32_t requestId, BotProtocol::EntityType type, uint32_t id)
{
  byte_t message[sizeof(BotProtocol::Header) + sizeof(BotProtocol::Entity)];
  BotProtocol::Header* header = (BotProtocol::Header*)message;
  BotProtocol::Entity* entity = (BotProtocol::Entity*)(header + 1);
  header->size = sizeof(message);
  header->messageType = BotProtocol::removeEntity;
  header->requestId = requestId;
  entity->entityType = type;
  entity->entityId = id;
  client.send(message, sizeof(message));
}

void_t ClientHandler::sendErrorResponse(BotProtocol::MessageType messageType, uint32_t requestId, const BotProtocol::Entity* entity, const String& errorMessage)
{
  BotProtocol::ErrorResponse errorResponse;
  errorResponse.messageType = messageType;
  if(entity)
  {
    errorResponse.entityType = entity->entityType;
    errorResponse.entityId = entity->entityId;
  }
  else
  {
    errorResponse.entityType = BotProtocol::none;
    errorResponse.entityId = 0;
  }
  BotProtocol::setString(errorResponse.errorMessage, errorMessage);
  sendMessage(BotProtocol::errorResponse, requestId, &errorResponse, sizeof(errorResponse));
}

void_t ClientHandler::sendRemoveAllEntities(BotProtocol::EntityType type)
{
  BotProtocol::Entity entity;
  entity.entityId = 0;
  entity.entityType = type;
  sendMessage(BotProtocol::removeAllEntities, 0, &entity, sizeof(entity));
}
