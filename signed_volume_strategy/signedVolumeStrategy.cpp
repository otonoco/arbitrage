#ifdef _WIN32
    #include "stdafx.h"
#endif

#include "signedVolumeStrategy.h"

#include "FillInfo.h"
#include "AllEventMsg.h"
#include "ExecutionTypes.h"
#include <Utilities/Cast.h>
#include <Utilities/utils.h>

#include <math.h>
#include <vector>
#include <iostream>
#include <cassert>

using namespace RCM::StrategyStudio;
using namespace RCM::StrategyStudio::MarketModels;
using namespace RCM::StrategyStudio::Utilities;

using namespace std;

SignedVolumeTrade::SignedVolumeTrade(StrategyID strategyID, const std::string& strategyName, const std::string& groupName):
    Strategy(strategyID, strategyName, groupName),
    volume_map(),
    m_instrument_order_id_map(),

    v_signedVolume(0),
    m_aggressiveness(0.01),
    m_position_size(100),
    m_debug_on(false),
    m_super_long_window_size(20)

{
    //this->set_enabled_pre_open_data_flag(true);
    //this->set_enabled_pre_open_trade_flag(true);
    //this->set_enabled_post_close_data_flag(true);
    //this->set_enabled_post_close_trade_flag(true);
}

SignedVolumeTrade::~SignedVolumeTrade() {

}


void SignedVolumeTrade::OnResetStrategyState() {
    volume_map.clear();
    m_instrument_order_id_map.clear();
    m_instrument_map.clear();
    v_signedVolume = 0;
    m_price_map.clear();
    m_size_map.clear();
}


void SignedVolumeTrade::DefineStrategyParams() {
    CreateStrategyParamArgs arg1("aggressiveness", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, m_aggressiveness);
    params().CreateParam(arg1);

    CreateStrategyParamArgs arg2("position_size", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_INT, m_position_size);
    params().CreateParam(arg2);

    CreateStrategyParamArgs arg3("super_long_window_size", STRATEGY_PARAM_TYPE_STARTUP, VALUE_TYPE_INT, m_super_long_window_size);
    params().CreateParam(arg3);
    
    CreateStrategyParamArgs arg4("debug", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_BOOL, m_debug_on);
    params().CreateParam(arg4);
}


void SignedVolumeTrade::DefineStrategyCommands() {
    StrategyCommand command1(1, "Reprice Existing Orders");
    commands().AddCommand(command1);

    StrategyCommand command2(2, "Cancel All Orders");
    commands().AddCommand(command2);
}


void SignedVolumeTrade::RegisterForStrategyEvents(StrategyEventRegister* eventRegister, DateType currDate) {    
    for (SymbolSetConstIter it = symbols_begin(); it != symbols_end(); ++it) {
        eventRegister->RegisterForBars(*it, BAR_TYPE_TIME, 600);
        EventInstrumentPair retVal = eventRegister->RegisterForMarketData(*it);
        m_instrument_map[*it] = retVal.second;
    }
}


void SignedVolumeTrade::OnTrade(const TradeDataEventMsg& msg) {
    const SymbolTag& symbol = msg.instrument().symbol();
    m_price_map[symbol] = msg.trade().price();
    SendOrder(&msg.instrument(), m_size_map[symbol]);
}


void SignedVolumeTrade::OnQuote(const QuoteEventMsg& msg) {
    const SymbolTag& symbol = msg.instrument().symbol();
    const IAggrOrderBook& orderBook = m_instrument_map[symbol]->aggregate_order_book();

    VolumeMapIterator iter = volume_map.find(&msg.instrument());

    if (iter != volume_map.end()) {
        v_signedVolume = &iter->second;
    } else {
        v_signedVolume = &volume_map.insert(make_pair(&msg.instrument(), SignedVolume(m_super_long_window_size))).first->second;
    }

    double ask1 = 0;
    double bid1 = 0;

    double ask2 = 0;
    double bid2 = 0;

    double ask3 = 0;
    double bid3 = 0;

    int ask_vol1 = 0;
    int bid_vol1 = 0;

    int ask_vol2 = 0;
    int bid_vol2 = 0;
    
    int ask_vol3 = 0;
    int bid_vol3 = 0;

    if (orderBook.AskPriceLevelAtLevel(0)!= NULL && orderBook.BidPriceLevelAtLevel(0)!= NULL) {
        ask1 = orderBook.AskPriceLevelAtLevel(0)->price();
        bid1 = orderBook.BidPriceLevelAtLevel(0)->price();

        ask_vol1 = orderBook.AskPriceLevelAtLevel(0)->size();
        bid_vol1 = orderBook.BidPriceLevelAtLevel(0)->size();
    }

    if (orderBook.AskPriceLevelAtLevel(1)!= NULL && orderBook.BidPriceLevelAtLevel(1)!= NULL) {
        ask2 = orderBook.AskPriceLevelAtLevel(1)->price();
        bid2 = orderBook.BidPriceLevelAtLevel(1)->price();

        ask_vol2 = orderBook.AskPriceLevelAtLevel(1)->size();
        bid_vol2 = orderBook.BidPriceLevelAtLevel(1)->size();
    }

    if (orderBook.AskPriceLevelAtLevel(2)!= NULL && orderBook.BidPriceLevelAtLevel(2)!= NULL) {
        ask3 = orderBook.AskPriceLevelAtLevel(2)->price();
        bid3 = orderBook.BidPriceLevelAtLevel(2)->price();

        ask_vol3 = orderBook.AskPriceLevelAtLevel(2)->size();
        bid_vol3 = orderBook.BidPriceLevelAtLevel(2)->size();
    }

    double midway = m_price_map[symbol];

    double weighted_sell = (ask1 * ask_vol1 + ask2 * ask_vol2 + ask3 * ask_vol3) / (ask_vol1 + ask_vol2 + ask_vol3);
    double weighted_buyy = (bid1 * bid_vol1 + bid2 * bid_vol2 + bid3 * bid_vol3) / (bid_vol1 + bid_vol2 + bid_vol3);
    
    double signed_value = abs(weighted_sell - midway) * (bid_vol1 + bid_vol2 + bid_vol3) - abs(midway - weighted_buyy) * (ask_vol1 + ask_vol2 + ask_vol3);
    DesiredPositionSide side = v_signedVolume->Update(signed_value);

    if (v_signedVolume->FullyInitialized()) {
        m_size_map[symbol] = m_position_size * side;
    }
}


void SignedVolumeTrade::OnOrderUpdate(const OrderUpdateEventMsg& msg) {    
	// std::cout << "OnOrderUpdate(): " << msg.update_time() << msg.name() << std::endl;
    if (msg.completes_order()) {
		m_instrument_order_id_map[msg.order().instrument()] = 0;
		// std::cout << "OnOrderUpdate(): order is complete; " << std::endl;
    }
}


void SignedVolumeTrade::AdjustPortfolio(const Instrument* instrument, int desired_position, double current_price) {
    int trade_size = 0;
    if (abs(desired_position + portfolio().position(instrument)) * current_price >= 500000){
        trade_size = 0;
    } else {
        trade_size = desired_position;
    }
    if (trade_size != 0) {
        OrderID order_id = m_instrument_order_id_map[instrument];
        if (order_id == 0) {
            SendOrder(instrument, trade_size);
        } else {  
            const Order* order = orders().find_working(order_id);
            if (order && ((IsBuySide(order->order_side()) && trade_size < 0) || ((IsSellSide(order->order_side()) && trade_size > 0)))) {
                trade_actions()->SendCancelOrder(order_id);
            }
        }
    }
}


void SignedVolumeTrade::FlashSale(const Instrument* instrument, int trade_size) {
    m_aggressiveness = 0.00;
    double price = trade_size > 0 ? instrument->top_quote().bid() + m_aggressiveness : instrument->top_quote().ask() - m_aggressiveness;

    OrderParams params(*instrument, 
        abs(trade_size),
        price, 
        (instrument->type() == INSTRUMENT_TYPE_EQUITY) ? MARKET_CENTER_ID_NASDAQ : ((instrument->type() == INSTRUMENT_TYPE_OPTION) ? MARKET_CENTER_ID_CBOE_OPTIONS : MARKET_CENTER_ID_CME_GLOBEX),
        (trade_size > 0) ? ORDER_SIDE_BUY : ORDER_SIDE_SELL,
        ORDER_TIF_DAY,
        ORDER_TYPE_MARKET);

    if (trade_actions()->SendNewOrder(params) == TRADE_ACTION_RESULT_SUCCESSFUL) {
        m_instrument_order_id_map[instrument] = params.order_id;
    }
}


void SignedVolumeTrade::SendOrder(const Instrument* instrument, int trade_size) {
    m_aggressiveness = 0.01;
    double price = trade_size > 0 ? instrument->top_quote().bid() + m_aggressiveness : instrument->top_quote().ask() - m_aggressiveness;

    OrderParams params(*instrument, 
        abs(trade_size),
        price, 
        (instrument->type() == INSTRUMENT_TYPE_EQUITY) ? MARKET_CENTER_ID_NASDAQ : ((instrument->type() == INSTRUMENT_TYPE_OPTION) ? MARKET_CENTER_ID_CBOE_OPTIONS : MARKET_CENTER_ID_CME_GLOBEX),
        (trade_size > 0) ? ORDER_SIDE_BUY : ORDER_SIDE_SELL,
        ORDER_TIF_DAY,
        ORDER_TYPE_LIMIT);

    // std::cout << "SendOrder(): about to send new order for " << trade_size << " at $" << price << std::endl;
    if (trade_actions()->SendNewOrder(params) == TRADE_ACTION_RESULT_SUCCESSFUL) {
        m_instrument_order_id_map[instrument] = params.order_id;
        // std::cout << "SendOrder(): Sending new order successful!" << std::endl;
    }
}


void SignedVolumeTrade::OnBar(const BarEventMsg& msg) {
    std::cout<<"______________Porfolio Information snapshot every hour_______________________"<<std::endl;    
    std::cout<< "Time "<<msg.bar_time()<<std::endl;
    std::cout<<" PnL "<<portfolio().total_pnl()<<std::endl;
    for (SymbolSetConstIter it = symbols_begin(); it != symbols_end(); ++it) {
        std::cout<<"Symbol "<<*it <<"   Position "<<portfolio().position(m_instrument_map[*it])<<std::endl;
    }

    std::stringstream ss;
	ss << msg.bar_time();
    std::ofstream outFile;
    outFile.open("account.txt");
    outFile << ss.str();
    outFile << portfolio().total_pnl();
}


void SignedVolumeTrade::RepriceAll() {
    for (IOrderTracker::WorkingOrdersConstIter ordit = orders().working_orders_begin(); ordit != orders().working_orders_end(); ++ordit) {
        Reprice(*ordit);
    }
}


void SignedVolumeTrade::Reprice(Order* order) {
    OrderParams params = order->params();
    params.price = (order->order_side() == ORDER_SIDE_BUY) ? order->instrument()->top_quote().bid() + m_aggressiveness : order->instrument()->top_quote().ask() - m_aggressiveness;
    trade_actions()->SendCancelReplaceOrder(order->order_id(), params);
}


void SignedVolumeTrade::OnStrategyCommand(const StrategyCommandEventMsg& msg) {
    switch (msg.command_id()) {
        case 1:
            RepriceAll();
            break;
        case 2:
            trade_actions()->SendCancelAll();
            break;
        default:
            logger().LogToClient(LOGLEVEL_DEBUG, "Unknown strategy command received");
            break;
    }
}


void SignedVolumeTrade::OnParamChanged(StrategyParam& param) {
    
}
