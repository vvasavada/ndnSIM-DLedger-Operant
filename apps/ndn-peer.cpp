#include "ndn-peer.hpp"
#include "ns3/random-variable-stream.h"
#include "ns3/ptr.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/callback.h"
#include "ns3/string.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/integer.h"
#include "ns3/double.h"

#include <stdlib.h>
#include "../ndn-cxx/src/util/sha256.hpp"
#include "../ndn-cxx/src/encoding/block-helpers.hpp"
#include "../ndn-cxx/src/encoding/tlv.hpp"
#include "ns3/ndnSIM/helper/ndn-stack-helper.hpp"

NS_LOG_COMPONENT_DEFINE("ndn.peer");

namespace ns3 {
namespace ndn {

NS_OBJECT_ENSURE_REGISTERED(Peer);

LedgerRecord::LedgerRecord(shared_ptr<const Data> contentObject,
                           int weight, int entropy, bool isArchived)
  : block(*contentObject)
  , weight(weight)
  , entropy(entropy)
  , isArchived(isArchived)
{

}

// register NS-3 Type
TypeId
Peer::GetTypeId()
{
  static TypeId tid = TypeId("Peer")
    .SetParent<App>()
    .AddConstructor<Peer>()
    //******** Variables to tune
    .AddAttribute("Frequency", "Frequency of record generation", IntegerValue(1),
                  MakeIntegerAccessor(&Peer::m_frequency), MakeIntegerChecker<int32_t>())
    .AddAttribute("SyncFrequency", "Frequency of sync interest multicast", IntegerValue(1),
                  MakeIntegerAccessor(&Peer::m_syncFrequency), MakeIntegerChecker<int32_t>())
    //.AddAttribute("WeightThreshold", "Weight to consider archive", IntegerValue(10),
    //              MakeIntegerAccessor(&Peer::m_weightThreshold), MakeIntegerChecker<int32_t>())
    .AddAttribute("MaxEntropy", "The max entropy a block can gain", IntegerValue(15),
                  MakeIntegerAccessor(&Peer::m_maxEntropy), MakeIntegerChecker<int32_t>())
    .AddAttribute("EntropyThreshold", "Entropy value of peers", IntegerValue(5),
                  MakeIntegerAccessor(&Peer::m_entropyThreshold), MakeIntegerChecker<int32_t>())
    .AddAttribute("GenesisNum", "Number of genesis blocks", IntegerValue(5),
                  MakeIntegerAccessor(&Peer::m_genesisNum), MakeIntegerChecker<int32_t>())
    .AddAttribute("ReferredNum", "Number of referred blocks", IntegerValue(2),
                  MakeIntegerAccessor(&Peer::m_referredNum), MakeIntegerChecker<int32_t>())
    //********
    .AddAttribute("Routable-Prefix", "Node's Prefix, for which producer has the data", StringValue("/"),
                  MakeNameAccessor(&Peer::m_routablePrefix), MakeNameChecker())
    .AddAttribute("Multicast-Prefix", "Multicast Prefix", StringValue("/dledger"),
                  MakeNameAccessor(&Peer::m_mcPrefix), MakeNameChecker())
    .AddAttribute("Randomize",
                  "Type of send time randomization: none (default), uniform, exponential",
                  StringValue("none"),
                  MakeStringAccessor(&Peer::SetGenerationRandomize, &Peer::GetGenerationRandomize),
                  MakeStringChecker())
    .AddAttribute("SyncRandomize",
                  "Type of sync randomization: none (default), uniform, exponential",
                  StringValue("none"),
                  MakeStringAccessor(&Peer::SetSyncRandomize, &Peer::GetSyncRandomize),
                  MakeStringChecker());
  return tid;
}

Peer::Peer()
  : m_firstTime(true)
  , m_syncFirstTime(true)
  , m_reqCounter(0)
{
}

std::vector<Name>
Peer::GetApprovedBlocks(Data data)
{
  std::vector<Name> approvedBlocks;
  auto content = ::ndn::encoding::readString(data.getContent());
  size_t pos = 0;
  pos = content.find("***");
  content = content.substr(0, pos);
  pos = 0;
  std::string approvedBlock;
  while ((pos = content.find(":")) != std::string::npos) {
    approvedBlock = content.substr(0, pos);
    Name approvedBlockName(approvedBlock);
    if (approvedBlockName.size() >= 2) { // ignoring empty strings when splitting (:tip1:tip2)
      approvedBlocks.push_back(approvedBlockName);
    }
    content.erase(0, pos + 1);
  }

  // the last token
  Name approvedBlockName(content);
  if (approvedBlockName.size() >= 2) { // ignoring empty strings when splitting (:tip1:tip2)
    approvedBlocks.push_back(approvedBlockName);
  }

  return approvedBlocks;
}

void
Peer::ScheduleNextGeneration()
{
  NS_LOG_FUNCTION_NOARGS();
  // double mean = 8.0 * m_payloadSize / m_desiredRate.GetBitRate ();
  // std::cout << "next: " << Simulator::Now().ToDouble(Time::S) + mean << "s\n";

  if (m_firstTime) {
    auto node = GetNode();
    int id = node->GetId();
    double startingPoint = id/100;
    m_sendEvent = Simulator::Schedule(Seconds(startingPoint), &Peer::GenerateRecord, this);
    m_firstTime = false;
  }
  else if (!m_sendEvent.IsRunning()) {
    if (m_frequency == 0) {
      m_sendEvent = Simulator::Schedule((m_random == 0) ? Seconds(1.0 / 1)
                                        : Seconds(m_random->GetValue()),
                                        &Peer::GenerateRecord, this);
    }
    else {
      m_sendEvent = Simulator::Schedule((m_random == 0) ? Seconds(1.0 / m_frequency)
                                        : Seconds(m_random->GetValue()),
                                        &Peer::GenerateRecord, this);
    }
  }
}

void
Peer::ScheduleNextSync()
{
  NS_LOG_FUNCTION_NOARGS();
  // double mean = 8.0 * m_payloadSize / m_desiredRate.GetBitRate ();
  // std::cout << "next: " << Simulator::Now().ToDouble(Time::S) + mean << "s\n";

  if (m_syncFirstTime) {
    auto node = GetNode();
    int id = node->GetId();
    double startingPoint = id/100;
    m_syncSendEvent = Simulator::Schedule(Seconds(startingPoint), &Peer::GenerateSync, this);
    m_syncFirstTime = false;
  }
  else if (!m_syncSendEvent.IsRunning()) {
    if (m_syncFrequency == 0) {
      m_syncSendEvent = Simulator::Schedule((m_syncRandom == 0) ? Seconds(1.0 / 1)
                                        : Seconds(m_syncRandom->GetValue()),
                                        &Peer::GenerateSync, this);
    }
    else {
      m_syncSendEvent = Simulator::Schedule((m_syncRandom == 0) ? Seconds(1.0 / m_syncFrequency)
                                        : Seconds(m_syncRandom->GetValue()),
                                        &Peer::GenerateSync, this);
    }
  }
}

void
Peer::SetGenerationRandomize(const std::string& value)
{
  if (value == "uniform") {
    m_random = CreateObject<UniformRandomVariable>();
    m_random->SetAttribute("Min", DoubleValue(0.0));
    m_random->SetAttribute("Max", DoubleValue(2 * 1.0 / m_frequency));
  }
  else if (value == "exponential") {
    m_random = CreateObject<ExponentialRandomVariable>();
    m_random->SetAttribute("Mean", DoubleValue(1.0 / m_frequency));
    m_random->SetAttribute("Bound", DoubleValue(50 * 1.0 / m_frequency));
  }
  else
    m_random = 0;

  m_randomTypeGeneration = value;
}

void
Peer::SetSyncRandomize(const std::string& value)
{
  if (value == "uniform") {
    m_syncRandom = CreateObject<UniformRandomVariable>();
    m_syncRandom->SetAttribute("Min", DoubleValue(0.0));
    m_syncRandom->SetAttribute("Max", DoubleValue(2 * 1.0 / m_syncFrequency));
  }
  else if (value == "exponential") {
    m_syncRandom = CreateObject<ExponentialRandomVariable>();
    m_syncRandom->SetAttribute("Mean", DoubleValue(1.0 / m_syncFrequency));
    m_syncRandom->SetAttribute("Bound", DoubleValue(50 * 1.0 / m_syncFrequency));
  }
  else
    m_syncRandom = 0;

  m_randomTypeSync = value;

}

std::string
Peer::GetGenerationRandomize() const
{
  return m_randomTypeGeneration;
}

std::string
Peer::GetSyncRandomize() const
{
  return m_randomTypeSync;
}

// Processing upon start of the application
void
Peer::StartApplication()
{
  NS_LOG_FUNCTION_NOARGS();
  App::StartApplication();
  FibHelper::AddRoute(GetNode(), m_routablePrefix, m_face, 0);
  FibHelper::AddRoute(GetNode(), m_mcPrefix, m_face, 0);

  // create genesis blocks in the DLedger
  for (int i = 0; i < m_genesisNum; i++) {
    Name genesisName(m_mcPrefix);
    genesisName.append("genesis");
    genesisName.append("genesis" + std::to_string(i));
    auto genesis = std::make_shared<Data>(genesisName);
    m_tipList.push_back(genesisName);
    m_ledger.insert(std::pair<Name, LedgerRecord>(genesisName, LedgerRecord(genesis)));
  }

  ScheduleNextGeneration();
  ScheduleNextSync();
}

// Processing when application is stopped
void
Peer::StopApplication()
{
  NS_LOG_FUNCTION_NOARGS();
  // cleanup App
  App::StopApplication();
}

// Triggers sync interest
void
Peer::GenerateSync()
{
  Name syncName(m_mcPrefix);
  syncName.append("SYNC");
  for (size_t i = 0; i != m_tipList.size(); i++) {
    syncName.append(m_tipList[i].toUri());
  }

  auto syncInterest = std::make_shared<Interest>(syncName);
  NS_LOG_INFO("> SYNC Interest " << syncInterest->getName().toUri());
  m_transmittedInterests(syncInterest, this, m_face);
  m_appLink->onReceiveInterest(*syncInterest);

  ScheduleNextSync();
}

// Generate a new record and send out notif and sync interest
void
Peer::GenerateRecord()
{
  std::set<Name> selectedBlocks;
  for (int i = 0; i < m_referredNum; i++) {
    auto referenceIndex = rand() % (m_tipList.size() - 1);
    auto reference = m_tipList.at(referenceIndex);
    bool isArchived = m_ledger.find(reference)->second.isArchived;

    // cannot select a block generated by myself
    // cannot select a confirmed block
    while (m_routablePrefix.isPrefixOf(reference) && isArchived) {
      referenceIndex = rand() % (m_tipList.size() - 1);
      reference = m_tipList.at(referenceIndex);
      isArchived = m_ledger.find(reference)->second.isArchived;
    }
    selectedBlocks.insert(reference);
  }

  std::string recordContent = "";
  for (const auto& item : selectedBlocks) {
    recordContent += ":";
    recordContent += item.toUri();

    m_tipList.erase(std::remove(m_tipList.begin(),
                                m_tipList.end(), item), m_tipList.end());
  }
  // to avoid the same digest made by multiple peers, add peer specific info
  recordContent += "***";
  recordContent += m_routablePrefix.toUri();

  // generate digest as a name component
  std::istringstream sha256Is(recordContent);
  ::ndn::util::Sha256 sha(sha256Is);
  std::string recordDigest = sha.toString();
  sha.reset();

  // generate a new record
  // Naming: /dledger/nodeX/digest
  Name recordName(m_routablePrefix);
  recordName.append(recordDigest);
  auto record = std::make_shared<Data>(recordName);
  record->setContent(::ndn::encoding::makeStringBlock(::ndn::tlv::Content, recordContent));
  ndn::StackHelper::getKeyChain().sign(*record);

  // attach to local ledger
  m_ledger.insert(std::pair<Name, LedgerRecord>(recordName, LedgerRecord(record)));
  // add to tip list
  m_tipList.push_back(recordName);

  // update weights of directly or indirectly approved blocks
  std::vector<Name> visited;
  UpdateWeightAndEntropy(*record, visited);

  Name notifName(m_mcPrefix);
  notifName.append("NOTIF").append(m_routablePrefix.getSubName(1).toUri()).append(recordDigest);
  auto notif = std::make_shared<Interest>(notifName);

  NS_LOG_INFO("> NOTIF Interest " << notif->getName().toUri());
  m_transmittedInterests(notif, this, m_face);
  m_appLink->onReceiveInterest(*notif);

  ScheduleNextGeneration();
}


// Update weights
void
Peer::UpdateWeightAndEntropy(Data tail, std::vector<Name> visited) {
  auto tailName = tail.getName();
  visited.push_back(tailName);
  if (tailName.toUri().find("genesis") != std::string::npos) {
    return;
  }

  std::vector<Name> approvedBlocks = GetApprovedBlocks(tail);
  std::vector<Name> processed;

  for (size_t i = 0; i != approvedBlocks.size(); i++) {
    auto approvedBlock = approvedBlocks[i];

    // this approvedblock shouldnt be previously processed to avoid increasing weight multiple times
    // (this condition is when multiple references point to same block)
    if (std::find(processed.begin(), processed.end(), approvedBlock) == processed.end()) {

      // do not increase weight if block has been previously visited
      // (this condition is useful when different chains merge)
      if (std::find(visited.begin(), visited.end(), approvedBlock) == visited.end()) {

        auto it = m_ledger.find(approvedBlock);
        if (it != m_ledger.end()) { // this should always return true
          it->second.weight += 1;
          it->second.approverNames.insert(tail.getName().getPrefix(3));
          it->second.entropy = it->second.approverNames.size();

          if (it->second.entropy >= m_entropyThreshold) {
            it->second.isArchived = true;
          }
        }
        processed.push_back(approvedBlock);
        UpdateWeightAndEntropy(m_ledger.find(approvedBlock)->second.block, visited);
      }
    }
  }
}

// Send out interest to fetch record
void
Peer::FetchRecord(Name recordName)
{
  auto recordInterest = std::make_shared<Interest>(recordName);
  m_transmittedInterests(recordInterest, this, m_face);
  NS_LOG_INFO("> RECORD Interest " << recordInterest->getName().toUri());
  m_appLink->onReceiveInterest(*recordInterest);
  m_reqCounter += 1;
}

// Callback that will be called when Data arrives
void
Peer::OnData(std::shared_ptr<const Data> data)
{
  NS_LOG_FUNCTION(this << data);
  auto dataName = data->getName();
  auto dataNameUri = dataName.toUri();
  // continue, if data is not just a reply to norif and sync interest
  if (dataNameUri.find("NOTIF") == std::string::npos && dataNameUri.find("SYNC") == std::string::npos) {
    m_reqCounter -= 1;
    bool approvedBlocksInLedger = true;
    //TODO: PoA verification (just assume it is correct? Then do nothing) Zhiyi: yes

    // Application-level semantics
    auto it = m_ledger.find(dataName);
    if (it != m_ledger.end()){
      return;
    }

    // Check if record doesn't refer to records generated by same producer
    // check if approved records have entropy less than max entropy
    // if not, check if record is in ledger
    // if not, retrieve it
    std::vector<Name> approvedBlocks = GetApprovedBlocks(*data);
    for (size_t i = 0; i != approvedBlocks.size(); i++) {
      auto approvedBlockName = approvedBlocks[i];
      if (approvedBlockName.size() >= 2) { // ignoring empty strings when splitting (:tip1:tip2)
        if (approvedBlockName.get(1) == dataName.get(1)) { // recordname format: /dledger/node/hash
          return;
        }
        it = m_ledger.find(approvedBlockName);
        if (it == m_ledger.end()) {
          approvedBlocksInLedger = false;
          FetchRecord(approvedBlockName);
          m_recordStack.push(LedgerRecord(data));
        } else {
          if (it->second.entropy > m_maxEntropy) {
            return;
          }
        }
      }
    }

    if (approvedBlocksInLedger && m_reqCounter == 0) {
      while (!m_recordStack.empty()) {
        auto record = m_recordStack.top();
        m_recordStack.pop();
        m_ledger.insert(std::pair<Name, LedgerRecord>(record.block.getName(), record));
        std::vector<Name> visited;
        UpdateWeightAndEntropy(record.block, visited);
      }
    }

    //TODO: the above records recursively obtained shouldnt be considered for approval when new 
    // record is generated
  }
}

// Callback that will be called when Interest arrives
void
Peer::OnInterest(std::shared_ptr<const Interest> interest)
{
  NS_LOG_INFO("< Interest " << interest->getName().toUri());
  auto interestName = interest->getName();
  auto interestNameUri = interestName.toUri();

  // if it is notification interest (/mc-prefix/NOTIF/creator-pref/name)
  if (interestNameUri.find("NOTIF") != std::string::npos) {
    Name recordName(m_mcPrefix);
    recordName.append(interestName.getSubName(2).toUri());
    FetchRecord(recordName);
  }
  // else if it is sync interest (/mc-prefix/SYNC/tip1/tip2 ...)
  // note that here tip1 will be /mc-prefix/creator-pref/name)
  else if (interestNameUri.find("SYNC") != std::string::npos) {
    auto tipDigest = interestName.getSubName(2);
    int iStartComponent = 0;
    auto tipName = tipDigest.getSubName(iStartComponent, 3);
    while (tipName.toUri() != "/") {
      auto it = m_ledger.find(tipName);
      if (it == m_ledger.end()) {
        FetchRecord(tipName);
      }
      else {
        // if weight is greater than 1,
        // this node has more recent tips
        // trigger sync
        if (it->second.weight > 1) {
          Name syncName(m_mcPrefix);
          syncName.append("SYNC");
          for (size_t i = 0; i != m_tipList.size(); i++) {
            syncName.append(m_tipList[i].toUri());
          }

          auto syncInterest = std::make_shared<Interest>(syncName);
          m_transmittedInterests(syncInterest, this, m_face);
          m_appLink->onReceiveInterest(*syncInterest);
        }
      }
      iStartComponent += 3;
      tipName = tipDigest.getSubName(iStartComponent, 3);
    }
  }
  // else it is record fetching interest
  else {
    auto it = m_ledger.find(interestName);
    if (it != m_ledger.end()){
      m_appLink->onReceiveData(it->second.block);
    }
    else {
      // This node doesn't have as well so it tries to fetch
      FetchRecord(interestName);
    }
  }
}

}
}
