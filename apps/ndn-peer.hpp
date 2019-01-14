#ifndef NDN_PEER_H
#define NDN_PEER_H

#include "ns3/ndnSIM/model/ndn-common.hpp"
#include "ns3/ndnSIM/apps/ndn-consumer.hpp"

#include <stack>

namespace ns3 {
namespace ndn {

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

  void SetGenerationRandomize(const std::string& value);

  void
  SetSyncRandomize(const std::string& value);

  std::string
  GetGenerationRandomize() const;

  std::string
  GetSyncRandomize() const;

private:
  // Get approved blocks from record content
  std::vector<Name>
  GetApprovedBlocks(Data data);

  // Generates new record and sends notif interest
  void
  GenerateRecord();

  // Triggers sync interest
  void
  GenerateSync();

  // Fetches record using the given prefix
  void
  FetchRecord(Name prefix);

  // Update weight of records
  void
  UpdateWeights(Data tail, std::vector<Name> visited);

protected:

  bool m_firstTime;
  bool m_syncFirstTime;
  Ptr<RandomVariableStream> m_random;
  Ptr<RandomVariableStream> m_syncRandom;
  std::string m_randomTypeGeneration;
  std::string m_randomTypeSync;
  EventId m_sendEvent; ///< @brief EventId of pending "send packet" event
  EventId m_syncSendEvent;

  std::vector<Name> m_tipList; // Tip list
  std::map<Name, Data> m_ledger; // A map name:record storing entire ledger
  std::map<Name, int> m_weightList; // A map name:weight storing record weights
  std::map<Name, int> m_entropyList; // A map name:entropy storing record entropy

  std::stack<Data> m_recordStack; // records stacked until their ancestors arrive
  int m_reqCounter; // request counter that talies record fetching interests sent with data received back

  // the var to tune
  double m_frequency; // Frequency of record generation (in hertz)
  double m_syncFrequency; // Frequency of sync interest multicast
  int m_weightThreshold; // weight to be considered as archived block
  int m_maxWeight; // max weight of a block which no new tips can refer
  int m_entropyThreshold; // the number of peers to approve
  int m_genesisNum; // the number of genesis blocks
  int m_referredNum; // the number of referred blocks

private:
  Name m_routablePrefix; // Node's prefix
  Name m_mcPrefix; // Multicast prefix
};

}
}

#endif
