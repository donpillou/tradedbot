
#pragma once

#include "Tools/Bot.h"

class ItemBot : public Bot
{
public:

private:
  class Session : public Bot::Session
  {
  public:
    struct Parameters
    {
      double sellProfitGain;
      double buyProfitGain;
      //double sellPriceGain;
      //double buyPriceGain;
    };

    Session(Broker& broker);

  private:
    Broker& broker;

    Parameters parameters;

    double minBuyInPrice;
    double maxSellInPrice;

    virtual ~Session() {}

    virtual void setParameters(double* parameters);

    virtual void handle(const DataProtocol::Trade& trade, const Values& values);
    virtual void handleBuy(const BotProtocol::Transaction& transaction);
    virtual void handleSell(const BotProtocol::Transaction& transaction);

    void checkBuy(const DataProtocol::Trade& trade, const Values& values);
    void checkSell(const DataProtocol::Trade& trade, const Values& values);
  };

public: // Bot
  virtual Session* createSession(Broker& broker) {return new Session(broker);};
  virtual unsigned int getParameterCount() const {return sizeof(Session::Parameters) / sizeof(double);}
};