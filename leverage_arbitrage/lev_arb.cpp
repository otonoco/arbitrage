#ifdef _WIN32
    #include "stdafx.h"
#endif

#include "lev_arb.h"

#include "FillInfo.h"
#include "AllEventMsg.h"
#include "ExecutionTypes.h"
#include <Utilities/Cast.h>
#include <Utilities/utils.h>

#include <math.h>
#include <iostream>
#include <cassert>

using namespace RCM::StrategyStudio;
using namespace RCM::StrategyStudio::MarketModels;
using namespace RCM::StrategyStudio::Utilities;

using namespace std;

LevArbStrategy::LevArbStrategy(StrategyID strategyID, const std::string& strategyName, const std::string& groupName):
    Strategy(strategyID, strategyName, groupName),
    m_spState(),
    m_bars(),
    m_instrumentX(NULL),
    m_instrumentY(NULL),
    m_tradeSize(1),
    m_nOrdersOutstanding(0),
    m_DebugOn(false),
	_lev_ratio(3),
	lastX(0.0),
	lastY(0.0),
	changeX(0.0),
	changeY(0.0) {

    m_spState.marketActive = true;

}

LevArbStrategy::~LevArbStrategy() {

}

void LevArbStrategy::OnResetStrategyState() {
    m_spState.marketActive = true;
    m_bars.clear();
}

void LevArbStrategy::DefineStrategyParams() {
    //CreateStrategyParamArgs arg1("z_score", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, m_zScoreThreshold);
    //params().CreateParam(arg1);

    CreateStrategyParamArgs arg2("trade_size", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_INT, m_tradeSize);
    params().CreateParam(arg2);

    CreateStrategyParamArgs arg3("debug", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_BOOL, m_DebugOn);
    params().CreateParam(arg3);
}

void LevArbStrategy::DefineStrategyGraphs() {
	//graphs().series().add("Mean");
    //graphs().series().add("ZScore");
}

void LevArbStrategy::RegisterForStrategyEvents(StrategyEventRegister* eventRegister, DateType currDate) {    
    int count = 0;

    for (SymbolSetConstIter it = symbols_begin(); it != symbols_end(); ++it) {
        EventInstrumentPair retVal = eventRegister->RegisterForBars(*it, BAR_TYPE_TIME, 10);    
        
        if (count == 0) {
            m_instrumentX = retVal.second;
        } else if (count == 1) {
            m_instrumentY = retVal.second;
        }
    
        ++count;
    }
}

void LevArbStrategy::OnTrade(const TradeDataEventMsg& msg) {

}

void LevArbStrategy::OnTopQuote(const QuoteEventMsg& msg) {

}

void LevArbStrategy::OnBar(const BarEventMsg& msg) {
     if (m_DebugOn) {
        ostringstream str;
        str << msg.instrument().symbol() << ": "<< msg.bar();
        logger().LogToClient(LOGLEVEL_DEBUG, str.str().c_str());
     }

    // update our bars collection
    m_bars[&msg.instrument()] = msg.bar();

    if (m_bars.size() < 2) {
	    //wait until we have bars for both pairs
        return;
    }

    assert(m_bars.size() == 2);

    if (lastX != 0 && lastY != 0) {
    	changeX = m_bars[m_instrumentX].close() / lastX - 1;
    	changeY = m_bars[m_instrumentY].close() / lastY - 1;
    } 
    lastX = m_bars[m_instrumentX].close();
    lastY = m_bars[m_instrumentY].close();
    
    // sell X and buy Y when changeX and changeY widens 
    if (changeX > 1.001 * _lev_ratio * changeY) {
        m_spState.unitsDesired = -m_tradeSize;
    } else if (changeX < -1.001 * _lev_ratio * changeY) {
        m_spState.unitsDesired = m_tradeSize;
    } else {
        m_spState.unitsDesired = 0;
    }

    if (m_spState.marketActive) {
        AdjustPortfolio();
    }
    
    m_bars.clear();
}

void LevArbStrategy::AdjustPortfolio() {
    // wait until orders are filled before we send out more orders
    if (orders().num_working_orders() > 0) { //|| abs(_lev_ratio * portfolio().position(m_instrumentX) * m_bars[m_instrumentX].close() + 
    		//portfolio().position(m_instrumentY) * m_bars[m_instrumentY].close() ) > 2) {
        return;
    }

    int sharesX = m_spState.unitsDesired * m_bars[m_instrumentY].close() - portfolio().position(m_instrumentX);
    int sharesY = m_spState.unitsDesired * _lev_ratio * m_bars[m_instrumentX].close() - portfolio().position(m_instrumentY);

    if (sharesX > 0) {
        SendBuyOrder(m_instrumentX, sharesX);
    } else if (sharesX < 0) {
        SendSellOrder(m_instrumentX, -sharesX);
    }
    if (sharesY > 0) {
        SendBuyOrder(m_instrumentY, sharesY);
    } else if (sharesY < 0) {
        SendSellOrder(m_instrumentY, -sharesY);
    }
}

void LevArbStrategy::SendBuyOrder(const Instrument* instrument, int unitsNeeded) {
    if (m_DebugOn) {
        std::stringstream ss;
        ss << "Sending buy order for " << instrument->symbol() << " at price " << instrument->top_quote().ask() << " and quantity " << unitsNeeded;   
        logger().LogToClient(LOGLEVEL_DEBUG, ss.str());
    }

    OrderParams params(*instrument, 
        unitsNeeded,
        (instrument->top_quote().ask() != 0) ? instrument->top_quote().ask() : instrument->last_trade().price(), 
        (instrument->type() == INSTRUMENT_TYPE_EQUITY) ? MARKET_CENTER_ID_NASDAQ : ((instrument->type() == INSTRUMENT_TYPE_OPTION) ? MARKET_CENTER_ID_CBOE_OPTIONS : MARKET_CENTER_ID_CME_GLOBEX),
        ORDER_SIDE_BUY,
        ORDER_TIF_DAY,
        ORDER_TYPE_MARKET);

    trade_actions()->SendNewOrder(params);
}
    
void LevArbStrategy::SendSellOrder(const Instrument* instrument, int unitsNeeded) {
    if (m_DebugOn) {
        std::stringstream ss;
        ss << "Sending sell order for " << instrument->symbol() << " at price " << instrument->top_quote().bid() << " and quantity " << unitsNeeded;   
        logger().LogToClient(LOGLEVEL_DEBUG, ss.str());
    }

    OrderParams params(*instrument, 
        unitsNeeded,
        (instrument->top_quote().bid() != 0) ? instrument->top_quote().bid() : instrument->last_trade().price(), 
        (instrument->type() == INSTRUMENT_TYPE_EQUITY) ? MARKET_CENTER_ID_NASDAQ : ((instrument->type() == INSTRUMENT_TYPE_OPTION) ? MARKET_CENTER_ID_CBOE_OPTIONS : MARKET_CENTER_ID_CME_GLOBEX),
        ORDER_SIDE_SELL,
        ORDER_TIF_DAY,
        ORDER_TYPE_MARKET);

    trade_actions()->SendNewOrder(params);
}

void LevArbStrategy::OnMarketState(const MarketStateEventMsg& msg) {

}

void LevArbStrategy::OnOrderUpdate(const OrderUpdateEventMsg& msg) {

}

void LevArbStrategy::OnAppStateChange(const AppStateEventMsg& msg) {

}

void LevArbStrategy::OnParamChanged(StrategyParam& param) {    
    //if (param.param_name() == "z_score") {
    //    if (!param.Get(&m_zScoreThreshold))
    //       throw StrategyStudioException("Could not get zscore threshold");
    //} else 
    if (param.param_name() == "trade_size") {
        if (!param.Get(&m_tradeSize))
            throw StrategyStudioException("Could not get trade size");
    } else if (param.param_name() == "debug") {
        if (!param.Get(&m_DebugOn))
            throw StrategyStudioException("Could not get trade size");
    }        
}

