
#pragma once

#ifndef _STRATEGY_STUDIO_LIB_EXAMPLES_SIMPLE_PAIRS_STRATEGY_H_
#define _STRATEGY_STUDIO_LIB_EXAMPLES_SIMPLE_PAIRS_STRATEGY_H_

#ifdef _WIN32
    #define _STRATEGY_EXPORTS __declspec(dllexport)
#else
    #ifndef _STRATEGY_EXPORTS
    #define _STRATEGY_EXPORTS
    #endif
#endif

#include <Strategy.h>
#include <Analytics/ScalarRollingWindow.h>
#include <MarketModels/Instrument.h>
#include <Utilities/ParseConfig.h>

#include <vector>
#include <map>
#include <iostream>

using namespace RCM::StrategyStudio;

struct StrategyLogicState {

    StrategyLogicState(): marketActive(0), unitsDesired(0) {}

    StrategyLogicState(int sunitsDesired): 
        marketActive(0), unitsDesired(unitsDesired)
    {
    }

    bool marketActive;
    int unitsDesired;    
};

class LevArbStrategy : public Strategy {
public:
    typedef std::map<const Instrument*, Bar> Bars; 
    typedef Bars::iterator BarsIter;
    typedef Bars::const_iterator BarsConstIter;

public:
    LevArbStrategy(StrategyID strategyID, const std::string& strategyName, const std::string& groupName);
    ~LevArbStrategy();

public: /* from IEventCallback */

    /**
     * This event triggers whenever trade message arrives from a market data source.
     */ 
    virtual void OnTrade(const TradeDataEventMsg& msg);

    /**
     * This event triggers whenever aggregate volume at best price changes, based 
     * on the best available source of liquidity information for the instrument.
     *
     * If the quote datasource only provides ticks that change the NBBO, top quote will be set to NBBO
     */ 
    virtual void OnTopQuote(const QuoteEventMsg& msg);

    /**
     * This event triggers whenever a new quote for a market center arrives from a consolidate or direct quote feed,
     * or when the market center's best price from a depth of book feed changes.
     *
     * User can check if quote is from consolidated or direct, or derived from a depth feed. This will not fire if
     * the data source only provides quotes that affect the official NBBO, as this is not enough information to accurately
     * mantain the state of each market center's quote.
     */ 
    virtual void OnQuote(const QuoteEventMsg& msg) {}
    
    /**
     * This event triggers whenever a order book message arrives. This will be the first thing that
     * triggers if an order book entry impacts the exchange's DirectQuote or Strategy Studio's TopQuote calculation.
     */ 
    virtual void OnDepth(const MarketDepthEventMsg& msg) {}

    /**
     * This event triggers whenever a Bar interval completes for an instrument
     */ 
    virtual void OnBar(const BarEventMsg& msg);

    /**
     * This event contains alerts about the state of the market
     */
    virtual void OnMarketState(const MarketStateEventMsg& msg);

    /**
     * This event triggers whenever new information arrives about a strategy's orders
     */ 
    virtual void OnOrderUpdate(const OrderUpdateEventMsg& msg);

    /**
     * This event contains strategy control commands arriving from the Strategy Studio client application (eg Strategy Manager)
     */ 
    virtual void OnStrategyControl(const StrategyStateControlEventMsg& msg) {}

    /**
     * This event contains alerts about the status of a market data source
     */ 
    virtual void OnDataSubscription(const DataSubscriptionEventMsg& msg) {}

    /**
     * This event contains alerts about the status of the Strategy Server process
     */ 
    virtual void OnAppStateChange(const AppStateEventMsg& msg);

    /**
    *  Perform additional reset for strategy state 
    */
    void OnResetStrategyState();

    /**
     * Notifies strategy for every succesfull change in the value of a strategy parameter.
     *
     * Will be called any time a new parameter value passes validation, including during strategy initialization when default parameter values
     * are set in the call to CreateParam and when any persisted values are loaded. Will also trigger after OnResetStrategyState
     * to remind the strategy of the current parameter values.
     */ 
    void OnParamChanged(StrategyParam& param);

private: // Helper functions specific to this strategy
    void AdjustPortfolio();
    void SendBuyOrder(const Instrument* instrument, int unitsNeeded);
    void SendSellOrder(const Instrument* instrument, int unitsNeeded);

private: /* from Strategy */
    
    virtual void RegisterForStrategyEvents(StrategyEventRegister* eventRegister, DateType currDate); 
    
    /**
     * Define any params for use by the strategy 
     */     
    virtual void DefineStrategyParams();

    /**
     * Provides an ideal place during strategy initialization to define custom strategy graphs using graphs().series().add(...) 
     */ 
    virtual void DefineStrategyGraphs();

private:
    StrategyLogicState m_spState;
    Bars m_bars;
    const MarketModels::Instrument* m_instrumentX;    
    const MarketModels::Instrument* m_instrumentY;
    //Analytics::ScalarRollingWindow<double> m_rollingWindow;
    //double m_zScore;
    //double m_zScoreThreshold;
    int m_tradeSize;
    int m_nOrdersOutstanding;
    bool m_DebugOn;
    double lastX, lastY, changeX, changeY;
    double _lev_ratio;
};

extern "C" {

    _STRATEGY_EXPORTS const char* GetType()
    {
        return "LevArbStrategy";
    }

    _STRATEGY_EXPORTS IStrategy* CreateStrategy(const char* strategyType, 
                                   unsigned strategyID, 
                                   const char* strategyName,
                                   const char* groupName)
    {
        if (strcmp(strategyType,GetType()) == 0) {
            return *(new LevArbStrategy(strategyID, strategyName, groupName));
        } else {
            return NULL;
        }
    }
        
     // must match an existing user within the system 
    _STRATEGY_EXPORTS const char* GetAuthor()
    {
        return "dlariviere";
    }

    // must match an existing trading group within the system 
    _STRATEGY_EXPORTS const char* GetAuthorGroup()
    {
        return "UIUC";
    }
    
    // used to ensure the strategy was built against a version of the SDK compatible with the server version
    _STRATEGY_EXPORTS const char* GetReleaseVersion()
    {
        return Strategy::release_version();
    }    
}

#endif
