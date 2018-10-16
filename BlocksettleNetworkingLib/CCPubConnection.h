#ifndef __CC_PUB_CONNECTION_H__
#define __CC_PUB_CONNECTION_H__

#include "CommonTypes.h"

#include <QObject>

#include <memory>
#include <string>

namespace spdlog {
   class logger;
}

class ConnectionManager;

class CCPubConnection : public QObject
{
Q_OBJECT

public:
   CCPubConnection(const std::shared_ptr<spdlog::logger>& logger
      , const std::shared_ptr<ConnectionManager>& connectionManager);
   ~CCPubConnection() noexcept override = default;

   CCPubConnection(const CCPubConnection&) = delete;
   CCPubConnection& operator = (const CCPubConnection&) = delete;

   CCPubConnection(CCPubConnection&&) = delete;
   CCPubConnection& operator = (CCPubConnection&&) = delete;

   bool LoadCCDefinitionsFromPub();

signals:
   void CCSecurityDef(bs::network::CCSecurityDef);
   void CCSecurityInfo(QString ccProd, QString ccDesc, unsigned long nbSatoshis, QString genesisAddr);

protected:
   virtual std::string GetPuBHost() const = 0;
   virtual std::string GetPuBPort() const = 0;
   virtual std::string GetPuBKey() const = 0;


   virtual bool IsTestNet() const = 0;

   virtual bool VerifySignature(const std::string& data, const std::string& signature) const = 0;

   bool SubmitRequestToPB(const std::string& name, const std::string& data);

   virtual void ProcessGenAddressesResponse(const std::string& response, bool sigVerified, const std::string &sig) = 0;
   virtual void ProcessSubmitAddrResponse(const std::string& response) = 0;

private:
   void OnDataReceived(const std::string& data);

   void ProcessErrorResponse(const std::string& responseString) const;

protected:
   std::shared_ptr<spdlog::logger>  logger_;
   int                              currentRev_ = 0;

private:
   std::shared_ptr<ConnectionManager>     connectionManager_;
};

#endif // __CC_PUB_CONNECTION_H__