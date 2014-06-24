
#include "TestBot.h"

void TestBot::Session::setParameters(double* parameters)
{
}

void TestBot::Session::handle(const DataProtocol::Trade& trade, const Values& values)
{
  if(updateCount++ == 0)
  {
    broker.warning("Executing bot test...");
    String message;
    message.printf("balance is: base=%f, comm=%f, fee=%f", broker.getBalanceBase(), broker.getBalanceComm(), broker.getFee());
    broker.warning(message);

    BotProtocol::SessionItem item;
    item.entityType = BotProtocol::sessionItem;
    item.initialType = BotProtocol::Transaction::buy;
    item.currentType = BotProtocol::Transaction::sell;
    item.date = 89;
    item.price = 300;
    item.amount = 0.02;
    item.price = 320;
    if(!broker.createItem(item))
      broker.warning("createItem returned false.");

    if(!broker.buy(300, 0.02, 30 * 1000))
      broker.warning("buy returned false.");
    if(!broker.sell(1000, 0.01, 25 * 1000))
      broker.warning("sell returned false.");
    if(broker.getOpenBuyOrderCount() != 1)
      broker.warning("buy order count is not 1.");
    if(broker.getOpenSellOrderCount() != 1)
      broker.warning("sell order count is not 1.");
    {
      List<BotProtocol::Transaction> transactions;
      broker.getTransactions(transactions);
      if(transactions.size() == 0)
        broker.warning("transactions size is 0.");
      else
      {
        BotProtocol::Transaction& transaction = transactions.front();
        double newAmount = transaction.amount / 2.;
        transaction.amount /= newAmount;
        broker.updateTransaction(transaction);
        size_t transactionCount = transactions.size();
        transactions.clear();
        broker.getTransactions(transactions);
        if(transactions.size() != transactionCount)
          broker.warning("transaction count changed after update.");
        BotProtocol::Transaction& transaction2 = transactions.front();
        if(transaction.amount != transaction2.amount)
          broker.warning("transaction update failed.");
        broker.removeTransaction(transaction2.entityId);
        transactions.clear();
        broker.getTransactions(transactions);
        if(transactions.size() != transactionCount - 1)
          broker.warning("transaction remove failed.");
      }
    }
    {
      List<BotProtocol::SessionItem> items;
      broker.getItems(items);
      if(items.size() == 0)
        broker.warning("items size is 0.");
      else
      {
        BotProtocol::SessionItem& item = items.front();
        double newAmount = item.amount / 2.;
        item.amount /= newAmount;
        broker.updateItem(item);
        size_t itemCount = items.size();
        items.clear();
        broker.getItems(items);
        if(items.size() != itemCount)
          broker.warning("item count changed after update.");
        BotProtocol::SessionItem& item2 = items.front();
        if(item.amount != item2.amount)
          broker.warning("item update failed.");
        broker.removeItem(item2.entityId);
        items.clear();
        broker.getItems(items);
        if(items.size() != itemCount - 1)
          broker.warning("item remove failed.");
      }
    }
    broker.warning("finished test.");
  }
}

void TestBot::Session::handleBuy(const BotProtocol::Transaction& transaction)
{
}

void TestBot::Session::handleSell(const BotProtocol::Transaction& transaction)
{
}
