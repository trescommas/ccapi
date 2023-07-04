#ifndef INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_HYPERLIQUID_H_
#define INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_HYPERLIQUID_H_
#ifdef CCAPI_ENABLE_SERVICE_MARKET_DATA
#ifdef CCAPI_ENABLE_EXCHANGE_HYPERLIQUID
#include "ccapi_cpp/service/ccapi_market_data_service.h"
namespace ccapi{
class MarketDataServiceHyperliquid: public MarketDataService {
    public:
      MarketDataServiceHyperliquid(std::function<void(Event&, Queue<Event>*)> eventHandler, SessionOptions sessionOptions, SessionConfigs sessionConfigs,
                            std::shared_ptr<ServiceContext> serviceContextPtr)
      : MarketDataService(eventHandler, sessionOptions, sessionConfigs, serviceContextPtr) {
            this->exchangeName = CCAPI_EXCHANGE_NAME_HYPERLIQUID;
            this->baseUrlWs = sessionConfigs.getUrlWebsocketBase().at(this->exchangeName) + "/ws";
            this->baseUrlRest = sessionConfigs.getUrlRestBase().at(this->exchangeName);
            this->setHostRestFromUrlRest(this->baseUrlRest);
            this->setHostWsFromUrlWs(this->baseUrlWs);
            try {
                this->tcpResolverResultsRest = this->resolver.resolve(this->hostRest, this->portRest);
                } catch (const std::exception& e) {
                CCAPI_LOGGER_FATAL(std::string("e.what() = ") + e.what());
                }
            #ifdef CCAPI_LEGACY_USE_WEBSOCKETPP
            #else
                try {
                this->tcpResolverResultsWs = this->resolverWs.resolve(this->hostWs, this->portWs);
                } catch (const std::exception& e) {
                CCAPI_LOGGER_FATAL(std::string("e.what() = ") + e.what());
                }
            #endif
        }
      virtual ~MarketDataServiceHyperliquid(){}

#ifndef CCAPI_EXPOSE_INTERNAL

    private:
#endif  
    void prepareSubscriptionDetail(std::string& channelId, std::string& symbolId, const std::string& field, const WsConnection& wsConnection,
                                 const Subscription& subscription, const std::map<std::string, std::string> optionMap) override {
                                    const auto& marketDepthRequested = std::stoi(optionMap.at(CCAPI_MARKET_DEPTH_MAX));
  }

  #ifdef CCAPI_LEGACY_USE_WEBSOCKETPP
  void pingOnApplicationLevel(wspp::connection_hdl hdl, ErrorCode& ec) override { this->send(hdl, "ping", wspp::frame::opcode::text, ec); }
  void onClose(wspp::connection_hdl hdl) override {
    WsConnection& wsConnection = this->getWsConnectionFromConnectionPtr(this->serviceContextPtr->tlsClientPtr->get_con_from_hdl(hdl));
    this->priceByConnectionIdChannelIdSymbolIdPriceIdMap.erase(wsConnection.id);
    MarketDataService::onClose(hdl);
  }
  #endif
  std::vector<std::string> createSendStringList(const WsConnection& wsConnection) override {
    std::vector<std::string> sendStringList;
    for (const auto& subscriptionListByChannelIdSymbolId : this->subscriptionListByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id)) {
      auto channelId = subscriptionListByChannelIdSymbolId.first;
      for (const auto& subscriptionListBySymbolId : subscriptionListByChannelIdSymbolId.second) {
        rj::Document document;
        document.SetObject();
        rj::Document::AllocatorType& allocator = document.GetAllocator();
        document.AddMember("method", rj::Value("subscribe").Move(), allocator);
        rj::Value args(rj::kObjectType);
        std::string symbolId = subscriptionListBySymbolId.first;
        std::string exchangeSubscriptionId = channelId + ":" + symbolId;
        this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_CHANNEL_ID] = channelId;
        this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_SYMBOL_ID] = symbolId;
        args.AddMember("coin", rj::Value(symbolId.c_str(), allocator).Move(), allocator);
        args.AddMember("type", rj::Value(channelId.c_str(), allocator).Move(), allocator);
        document.AddMember("subscription", args, allocator);
        rj::StringBuffer stringBuffer;
        rj::Writer<rj::StringBuffer> writer(stringBuffer);
        document.Accept(writer);
        std::string sendString = stringBuffer.GetString();
        sendStringList.push_back(sendString);
      }
    }
    return sendStringList;
  }
  void processTextMessage(
#ifdef CCAPI_LEGACY_USE_WEBSOCKETPP
      WsConnection& wsConnection, wspp::connection_hdl hdl, const std::string& textMessage
#else
      std::shared_ptr<WsConnection> wsConnectionPtr, boost::beast::string_view textMessageView
#endif
      ,
      const TimePoint& timeReceived, Event& event, std::vector<MarketDataMessage>& marketDataMessageList) override {
#ifdef CCAPI_LEGACY_USE_WEBSOCKETPP
#else
    WsConnection& wsConnection = *wsConnectionPtr;
    std::string textMessage(textMessageView);
      if (textMessage != "Websocket connection established.") {
        rj::Document document;
        document.Parse<rj::kParseNumbersAsStringsFlag>(textMessage.c_str());
        std::string channelId = document["channel"].GetString();
        if (channelId == CCAPI_WEBSOCKET_HYPERLIQUID_CHANNEL_MARKET_DEPTH) {
            MarketDataMessage::RecapType recapType = MarketDataMessage::RecapType::SOLICITED;
            const rj::Value data = document["data"].GetObject();
            std::string symbolId = data["coin"].GetString();
            std::string exchangeSubscriptionId = channelId + ":" + symbolId;
            MarketDataMessage marketDataMessage;
            marketDataMessage.type = MarketDataMessage::Type::MARKET_DATA_EVENTS_MARKET_DEPTH;
            marketDataMessage.recapType = recapType;
            marketDataMessage.exchangeSubscriptionId = exchangeSubscriptionId;
            marketDataMessage.tp = TimePoint(std::chrono::milliseconds(std::stoll(data["time"].GetString())));
            rj::GenericArray levels = data["levels"].GetArray();
            for (const auto& y : levels[0].GetArray()) {
                MarketDataMessage::TypeForDataPoint dataPoint;
                dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(y["px"].GetString())});
                dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(y["sz"].GetString())});
                marketDataMessage.data[MarketDataMessage::DataType::BID].emplace_back(std::move(dataPoint));
            }
            for (const auto& y : levels[1].GetArray()) {
                MarketDataMessage::TypeForDataPoint dataPoint;
                dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(y["px"].GetString())});
                dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(y["sz"].GetString())});
                marketDataMessage.data[MarketDataMessage::DataType::ASK].emplace_back(std::move(dataPoint));
            }
            marketDataMessageList.emplace_back(std::move(marketDataMessage));
        }
      }
    }
};
}

#endif
#endif
#endif
#endif  // INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_HYPERLIQUID_H_