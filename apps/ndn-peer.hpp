#ifndef NDN_PEER_H
#define NDN_PEER_H

#include "ns3/ndnSIM/model/ndn-common.hpp"
#include "ns3/ndnSIM/apps/ndn-consumer.hpp"

#include <stack>
#include <list>

namespace ns3 {
namespace ndn {

class LedgerRecord
{
public:
  LedgerRecord(shared_ptr<const Data> contentObject,
               int weight = 1, int entropy = 0, bool isArchived = false);
public:
  shared_ptr<const Data> block;
  int weight = 1;
  int entropy = 0;
  std::set<std::string> approverNames;
  bool isArchived = false;

public:
  bool isASample = false;
  Time creationTime;
};

class Peer: public App
{
public:
  // register NS-3 type "Peer"
  static TypeId
  GetTypeId();

  Peer();
  virtual ~Peer(){};

  // (overridden from App) Callback that will be called when Data arrives
  virtual void
  OnData(shared_ptr<const Data> contentObject);

  // (overridden from App) Callback that will be called when Interest arrives
  virtual void
  OnInterest(shared_ptr<const Interest> interest);

protected:
  // (overridden from App) Processing upon start of the application
  virtual void
  StartApplication();

  // (overridden from App) Processing when application is stopped
  virtual void
  StopApplication();

  // schedule next record generation
  virtual void
  ScheduleNextGeneration();

  // schedule next sync
  virtual void
  ScheduleNextSync();

  void
  SetRandomize(const std::string& value, double frequency);

  void
  SetGenerationRandomize(const std::string& value);

  void
  SetSyncRandomize(const std::string& value);

  std::string
  GetGenerationRandomize() const;

  std::string
  GetSyncRandomize() const;

public:
  // Get approved blocks from record content
  std::vector<std::string>
  GetApprovedBlocks(shared_ptr<const Data> data);

  //Generates revocation record
  void
  GenerateRevocation(std::string revoked_node);

private:

  // Generates new record and sends notif interest
  void
  GenerateRecord();

  /// Helper functions for revocation and record generation ///
  std::set<std::string> 
  SelectApprovals(bool revocation);

  std::string 
  BuildRecordContent(std::set<std::string> selectedBlocks, std::string specific_info);

  void 
  GenerateRecordDataAndNotify(std::string recordContent, bool revocation);

  // Adds revocation to blackList
  void
  AddRevocation(shared_ptr<const Data> data);

  // Triggers sync interest
  void
  GenerateSync();

  // Fetches record using the given prefix
  void
  FetchRecord(Name prefix);

  // Update weight of records
  void
  UpdateWeightAndEntropy(shared_ptr<const Data> tail, std::set<std::string>& visited, std::string nodeName);

protected:

  bool m_firstTime;
  bool m_syncFirstTime;
  Ptr<RandomVariableStream> m_random;
  Ptr<RandomVariableStream> m_syncRandom;
  std::string m_randomTypeGeneration;
  std::string m_randomTypeSync;
  EventId m_sendEvent; ///< @brief EventId of pending "send packet" event
  EventId m_syncSendEvent;

  std::vector<std::string> m_tipList; // Tip list
  std::map<std::string, LedgerRecord> m_ledger;

  std::list<LedgerRecord> m_recordStack; // records stacked until their ancestors arrive
  std::set<std::string> m_missingRecords;
  int m_reqCounter; // request counter that talies record fetching interests sent with data received back
  
  std::vector<std::string> m_blackList; // list of nodes whose certificates has been revoked

  // the var to tune
  double m_frequency; // Frequency of record generation (in hertz)
  double m_syncFrequency; // Frequency of sync interest multicast
  int m_weightThreshold; // weight to be considered as archived block
  int m_conEntropy; // max entropy of a block which no new tips can refer
  int m_entropyThreshold; // the number of peers to approve
  int m_genesisNum; // the number of genesis blocks
  int m_referredNum; // the number of referred blocks

private:
  Name m_routablePrefix; // Node's prefix
  Name m_mcPrefix; // Multicast prefix
  Name m_idManagerPrefix; // Identity Manager's Prefix

  std::string m_lastRevocation; // to be used by identity manager

public:
  std::map<std::string, LedgerRecord> & GetLedger() {
    return m_ledger;
  }
};

}
}

#endif
