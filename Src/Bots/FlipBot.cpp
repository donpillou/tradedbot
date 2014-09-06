
#include <nstd/Map.h>
#include <nstd/Math.h>

#include "FlipBot.h"

#define DEFAULT_BUY_PROFIT_GAIN 0.4
#define DEFAULT_SELL_PROFIT_GAIN 0.4
#define DEFAULT_BUY_COOLDOWN (60 * 60)
#define DEFAULT_BUY_TIMEOUT (60 * 60)
#define DEFAULT_SELL_COOLDOWN (60 * 60)
#define DEFAULT_SELL_TIMEOUT (60 * 60)

FlipBot::Session::Session(Broker& broker) : broker(broker)//, minBuyInPrice(0.), maxSellInPrice(0.)
{
  updateBalance();

  broker.registerProperty("Sell Profit Gain", DEFAULT_SELL_PROFIT_GAIN);
  broker.registerProperty("Buy Profit Gain", DEFAULT_BUY_PROFIT_GAIN);
  broker.registerProperty("Buy Cooldown", DEFAULT_BUY_COOLDOWN);
  broker.registerProperty("Buy Timeout", DEFAULT_BUY_TIMEOUT);
  broker.registerProperty("Sell Cooldown", DEFAULT_SELL_COOLDOWN);
  broker.registerProperty("Sell Timeout", DEFAULT_SELL_TIMEOUT);
}

void_t FlipBot::Session::updateBalance()
{
  double balanceBase = 0.;
  double balanceComm = 0.;
  const HashMap<uint32_t, BotProtocol::SessionAsset>& assets = broker.getAssets();
  for(HashMap<uint32_t, BotProtocol::SessionAsset>::Iterator i = assets.begin(), end = assets.end(); i != end; ++i)
  {
    const BotProtocol::SessionAsset& asset = *i;
    balanceComm += asset.balanceComm;
    balanceBase += asset.balanceBase;
  }
  broker.setProperty(String("Balance ") + broker.getCurrencyBase(), balanceBase, BotProtocol::SessionProperty::readOnly, broker.getCurrencyBase());
  broker.setProperty(String("Balance ") + broker.getCurrencyComm(), balanceComm, BotProtocol::SessionProperty::readOnly, broker.getCurrencyComm());
}

void FlipBot::Session::handleTrade(const DataProtocol::Trade& trade, const Values& values)
{
  checkBuy(trade, values);
  checkSell(trade, values);
}

void FlipBot::Session::handleBuy(uint32_t orderId, const BotProtocol::Transaction& transaction2)
{
  String message;
  message.printf("Bought %.08f @ %.02f", transaction2.amount, transaction2.price);
  broker.warning(message);

  const HashMap<uint32_t, BotProtocol::SessionAsset>& assets = broker.getAssets();
  for(HashMap<uint32_t, BotProtocol::SessionAsset>::Iterator i = assets.begin(), end = assets.end(); i != end; ++i)
  {
    const BotProtocol::SessionAsset& asset = *i;
    if(asset.state == BotProtocol::SessionAsset::buying && asset.orderId == orderId)
    {
      BotProtocol::SessionAsset updatedAsset = asset;
      updatedAsset.state = BotProtocol::SessionAsset::waitSell;
      updatedAsset.orderId = 0;
      updatedAsset.price = transaction2.price;
      updatedAsset.balanceComm += transaction2.amount;
      updatedAsset.balanceBase -= transaction2.total;
      //double fee = broker.getFee();
      double fee = 0.005;
      updatedAsset.profitablePrice = transaction2.price * (1. + fee * 2.);
      double sellProfitGain = broker.getProperty("Sell Profit Gain", DEFAULT_SELL_PROFIT_GAIN);
      updatedAsset.flipPrice = transaction2.price * (1. + fee * (1. + sellProfitGain) * 2.);
      updatedAsset.date = transaction2.date;

      broker.updateAsset(updatedAsset);
      updateBalance();
      break;
    }
  }
}

void FlipBot::Session::handleSell(uint32_t orderId, const BotProtocol::Transaction& transaction2)
{
  String message;
  message.printf("Sold %.08f @ %.02f", transaction2.amount, transaction2.price);
  broker.warning(message);

  const HashMap<uint32_t, BotProtocol::SessionAsset>& assets = broker.getAssets();
  for(HashMap<uint32_t, BotProtocol::SessionAsset>::Iterator i = assets.begin(), end = assets.end(); i != end; ++i)
  {
    const BotProtocol::SessionAsset& asset = *i;
    if(asset.state == BotProtocol::SessionAsset::selling && asset.orderId == orderId)
    {
      BotProtocol::SessionAsset updatedAsset = asset;
      updatedAsset.state = BotProtocol::SessionAsset::waitBuy;
      updatedAsset.orderId = 0;
      updatedAsset.price = transaction2.price;
      updatedAsset.balanceComm -= transaction2.amount;
      updatedAsset.balanceBase += transaction2.total;
      //double fee = broker.getFee();
      double fee = 0.005;
      updatedAsset.profitablePrice = transaction2.price / (1. + fee * 2.);
      double buyProfitGain = broker.getProperty("Buy Profit Gain", DEFAULT_BUY_PROFIT_GAIN);
      updatedAsset.flipPrice = transaction2.price / (1. + fee * (1. + buyProfitGain) * 2.);
      updatedAsset.date = transaction2.date;

      broker.updateAsset(updatedAsset);
      updateBalance();
      break;
    }
  }
}

void_t FlipBot::Session::handleBuyTimeout(uint32_t orderId)
{
  const HashMap<uint32_t, BotProtocol::SessionAsset>& assets = broker.getAssets();
  for(HashMap<uint32_t, BotProtocol::SessionAsset>::Iterator i = assets.begin(), end = assets.end(); i != end; ++i)
  {
    const BotProtocol::SessionAsset& asset = *i;
    if(asset.state == BotProtocol::SessionAsset::buying && asset.orderId == orderId)
    {
      BotProtocol::SessionAsset updatedAsset = asset;
      updatedAsset.state = BotProtocol::SessionAsset::waitBuy;
      updatedAsset.orderId = 0;
      broker.updateAsset(updatedAsset);
      break;
    }
  }
}

void_t FlipBot::Session::handleSellTimeout(uint32_t orderId)
{
  const HashMap<uint32_t, BotProtocol::SessionAsset>& assets = broker.getAssets();
  for(HashMap<uint32_t, BotProtocol::SessionAsset>::Iterator i = assets.begin(), end = assets.end(); i != end; ++i)
  {
    const BotProtocol::SessionAsset& asset = *i;
    if(asset.state == BotProtocol::SessionAsset::selling && asset.orderId == orderId)
    {
      BotProtocol::SessionAsset updatedAsset = asset;
      updatedAsset.state = BotProtocol::SessionAsset::waitSell;
      updatedAsset.orderId = 0;
      broker.updateAsset(updatedAsset);
      break;
    }
  }
}

void_t FlipBot::Session::handleAssetUpdate(const BotProtocol::SessionAsset& asset)
{
  updateBalance();
}

void_t FlipBot::Session::handleAssetRemoval(const BotProtocol::SessionAsset& asset)
{
  updateBalance();
}

void FlipBot::Session::checkBuy(const DataProtocol::Trade& trade, const Values& values)
{
  if(broker.getOpenBuyOrderCount() > 0)
    return; // there is already an open buy order
  timestamp_t buyCooldown = (timestamp_t)broker.getProperty("Buy Cooldown", DEFAULT_BUY_COOLDOWN);
  if(broker.getTimeSinceLastBuy() < buyCooldown * 1000)
    return; // do not buy too often

  double tradePrice = trade.price;
  const HashMap<uint32_t, BotProtocol::SessionAsset>& assets = broker.getAssets();
  for(HashMap<uint32_t, BotProtocol::SessionAsset>::Iterator i = assets.begin(), end = assets.end(); i != end; ++i)
  {
    const BotProtocol::SessionAsset& asset = *i;
    if(asset.state == BotProtocol::SessionAsset::waitBuy && tradePrice <= asset.flipPrice)
    {
      BotProtocol::SessionAsset updatedAsset = asset;
      updatedAsset.state = BotProtocol::SessionAsset::buying;
      broker.updateAsset(updatedAsset);

      timestamp_t buyTimeout = (timestamp_t)broker.getProperty("Buy Timeout", DEFAULT_BUY_TIMEOUT);
      if(broker.buy(tradePrice, 0., asset.balanceBase, buyTimeout * 1000, &updatedAsset.orderId, 0))
        broker.updateAsset(updatedAsset);
      else
      {
        updatedAsset.state = BotProtocol::SessionAsset::waitBuy;
        broker.updateAsset(updatedAsset);
      }
      break;
    }
  }
}

void FlipBot::Session::checkSell(const DataProtocol::Trade& trade, const Values& values)
{
  if(broker.getOpenSellOrderCount() > 0)
    return; // there is already an open sell order
  timestamp_t sellCooldown = (timestamp_t)broker.getProperty("Sell Cooldown", DEFAULT_SELL_COOLDOWN);
  if(broker.getTimeSinceLastSell() < sellCooldown * 1000)
    return; // do not sell too often

  double tradePrice = trade.price;
  const HashMap<uint32_t, BotProtocol::SessionAsset>& assets = broker.getAssets();
  for(HashMap<uint32_t, BotProtocol::SessionAsset>::Iterator i = assets.begin(), end = assets.end(); i != end; ++i)
  {
    const BotProtocol::SessionAsset& asset = *i;
    if(asset.state == BotProtocol::SessionAsset::waitSell && tradePrice >= asset.flipPrice)
    {
      BotProtocol::SessionAsset updatedAsset = asset;
      updatedAsset.state = BotProtocol::SessionAsset::selling;
      broker.updateAsset(updatedAsset);

      timestamp_t sellTimeout = (timestamp_t)broker.getProperty("Sell Timeout", DEFAULT_SELL_TIMEOUT);
      if(broker.sell(tradePrice, asset.balanceComm, 0., sellTimeout * 1000, &updatedAsset.orderId, 0))
        broker.updateAsset(updatedAsset);
      else
      {
        updatedAsset.state = BotProtocol::SessionAsset::waitSell;
        broker.updateAsset(updatedAsset);
      }
      break;
    }
  }
}