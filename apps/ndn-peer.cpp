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
#include <math.h>
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
  : block(contentObject)
  , weight(weight)
  , entropy(entropy)
  , isArchived(isArchived)
{
  Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
  int num = static_cast<int>(x->GetValue()*100);
  //if (num > 95) {
  {
    isASample = true;
    creationTime = Simulator::Now();
  }
}

// register NS-3 Type
TypeId
Peer::GetTypeId()
{
  static TypeId tid = TypeId("Peer")
    .SetParent<App>()
    .AddConstructor<Peer>()
    //******** Variables to tune
    .AddAttribute("Frequency", "Frequency of record generation", DoubleValue(1.0),
                  MakeDoubleAccessor(&Peer::m_frequency), MakeDoubleChecker<double>())
    .AddAttribute("SyncFrequency", "Frequency of sync interest multicast", DoubleValue(1.0),
                  MakeDoubleAccessor(&Peer::m_syncFrequency), MakeDoubleChecker<double>())
    .AddAttribute("ConEntropy", "The max entropy a block can gain", IntegerValue(15),
                  MakeIntegerAccessor(&Peer::m_conEntropy), MakeIntegerChecker<int32_t>())
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
    .AddAttribute("Identity-Manager-Prefix", "Identity Manager's Prefix", StringValue("/"),
                  MakeNameAccessor(&Peer::m_idManagerPrefix), MakeNameChecker())
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
  //, m_reqCounter(0)
{
}

std::vector<std::string>
Peer::GetApprovedBlocks(shared_ptr<const Data> data)
{
  std::vector<std::string> approvedBlocks;
  auto content = ::ndn::encoding::readString(data->getContent());
  int nSlash = 0;
  const char *st, *ed;
  for(st = ed = content.c_str(); *ed && *ed != '*'; ed ++){
    if(*ed == ':'){
      if(nSlash >= 2){
        approvedBlocks.push_back(std::string(st, ed));
      }
      nSlash = 0;
      st = ed + 1;
    }else if(*ed == '/'){
      nSlash ++;
    }
  }
  if(nSlash >= 2){
    approvedBlocks.push_back(std::string(st, ed));
  }

  return approvedBlocks;
}


void
Peer::AddRevocation(shared_ptr<const Data> data)
{
  auto content = ::ndn::encoding::readString(data->getContent());
  size_t pos = 0;
  pos = content.find("***");
  content = content.substr(pos + 3);
  m_blackList.push_back(content);
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
    double startingPoint = id/5;
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
    double startingPoint = id/10;
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
    auto genesisNameStr = genesisName.toUri();
    m_tipList.push_back(genesisNameStr);
    m_ledger.insert(std::pair<std::string, LedgerRecord>(genesisNameStr, LedgerRecord(genesis)));
  }

  if (m_routablePrefix != m_idManagerPrefix) {
    ScheduleNextGeneration();
  } else {
    m_lastRevocation = m_mcPrefix.toUri() + "/genesis/genesis0";
  }

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
    syncName.append(m_tipList[i]);
  }

  auto syncInterest = std::make_shared<Interest>(syncName);
  NS_LOG_INFO("> SYNC Interest " << syncInterest->getName().toUri());
  m_transmittedInterests(syncInterest, this, m_face);
  m_appLink->onReceiveInterest(*syncInterest);

  ScheduleNextSync();
}

std::set<std::string>
Peer::SelectApprovals(bool revocation){
  std::set<std::string> selectedBlocks;
  int tryTimes = 0;
  for (int i = 0; i < m_referredNum; i++) {
    auto referenceIndex = rand() % (m_tipList.size() - 1);
    auto reference = m_tipList.at(referenceIndex);
    bool isArchived = m_ledger.find(reference)->second.isArchived;

    // cannot select a block generated by myself
    // cannot select a confirmed block
    while (m_routablePrefix.isPrefixOf(reference) || isArchived) {
      referenceIndex = rand() % (m_tipList.size() - 1);
      reference = m_tipList.at(referenceIndex);
      isArchived = m_ledger.find(reference)->second.isArchived;
    }
    selectedBlocks.insert(reference);
    if (i == m_referredNum - 1 && selectedBlocks.size() < 2) {
      i--;
      tryTimes++;
      if (tryTimes > 10) {
        NS_LOG_INFO("Try times used up: " << tryTimes);
        if (!revocation){
          ScheduleNextGeneration();
        }
        return std::set<std::string>();
      }
    }
  }
  if (revocation) {
    selectedBlocks.insert(m_lastRevocation);
  }
  return selectedBlocks;
}

std::string 
Peer::BuildRecordContent(std::set<std::string> selectedBlocks, std::string specific_info)
{
  std::string recordContent = "";
  for (const auto& item : selectedBlocks) {
    recordContent += ":";
    recordContent += item;
    m_tipList.erase(std::remove(m_tipList.begin(),
                                m_tipList.end(), item), m_tipList.end());
  }
  // to avoid the same digest made by multiple peers, add peer specific info
  recordContent += "***";
  recordContent += specific_info;

  return recordContent;
}

void
Peer::GenerateRecordDataAndNotify(std::string recordContent, bool revocation)
{

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
  m_ledger.insert(std::pair<std::string, LedgerRecord>(recordName.toUri(), LedgerRecord(record)));
  // add to tip list
  m_tipList.push_back(recordName.toUri());

  // update weights of directly or indirectly approved blocks
  std::set<std::string> visited;
  UpdateWeightAndEntropy(record, visited, recordName.getSubName(0, 2).toUri());
  NS_LOG_INFO("NewRecord: visited records size: " << visited.size()
              << " unconfirmed depth: " << log2(visited.size() + 1));

  Name notifName(m_mcPrefix);
  notifName.append("NOTIF").append(m_routablePrefix.getSubName(1).toUri()).append(recordDigest);
  auto notif = std::make_shared<Interest>(notifName);

  NS_LOG_INFO("> NOTIF Interest " << notif->getName().toUri());
  m_transmittedInterests(notif, this, m_face);
  m_appLink->onReceiveInterest(*notif);

  if (!revocation) {
    ScheduleNextGeneration();
  } else {
    m_lastRevocation = recordName.toUri();
  }
}
  
void
Peer::GenerateRevocation(std::string revoked_node)
{
  NS_LOG_FUNCTION_NOARGS();
  if (m_missingRecords.size() > 0) {
    NS_LOG_INFO("Missing record number: " << m_missingRecords.size());
    return;
  }

  auto selectedBlocks = SelectApprovals(true);
  if (selectedBlocks.empty()) {
    return;
  }

  auto recordContent = BuildRecordContent(selectedBlocks, revoked_node);

  GenerateRecordDataAndNotify(recordContent, true);

}

// Generate a new record and send out notif and sync interest
void
Peer::GenerateRecord()
{
  NS_LOG_FUNCTION_NOARGS();
  if (m_missingRecords.size() > 0) {
    NS_LOG_INFO("Missing record number: " << m_missingRecords.size());
    ScheduleNextGeneration();
    return;
  }

  auto selectedBlocks = SelectApprovals(false);
  if (selectedBlocks.empty()) {
    return;
  }

  auto recordContent = BuildRecordContent(selectedBlocks, m_routablePrefix.toUri());

  GenerateRecordDataAndNotify(recordContent, false);
}


// Update weights
void
Peer::UpdateWeightAndEntropy(shared_ptr<const Data> tail, std::set<std::string>& visited, std::string nodeName) {
  auto tailName = tail->getName().toUri();
  //std::cout << tail->getName().getSubName(0, 2).toUri() << std::endl;
  visited.insert(tailName);
  // std::cout << "visited set size: " << visited.size() << std::endl;

  std::vector<std::string> approvedBlocks = GetApprovedBlocks(tail);
  std::set<std::string> processed;

  for (size_t i = 0; i != approvedBlocks.size(); i++) {
    auto approvedBlock = approvedBlocks[i];

    // this approvedblock shouldnt be previously processed to avoid increasing weight multiple times
    // (this condition is when multiple references point to same block)
    auto search = processed.find(approvedBlock);
    if (search == processed.end()) {

      // do not increase weight if block has been previously visited
      // (this condition is useful when different chains merge)
      auto search2 = visited.find(approvedBlock);
      if (search2 == visited.end()) {

        auto it = m_ledger.find(approvedBlock);
        if (it != m_ledger.end()) { // this should always return true
          it->second.weight += 1;
          it->second.approverNames.insert(nodeName);
          it->second.entropy = it->second.approverNames.size();
          if (it->second.entropy >= m_entropyThreshold) {
            it->second.isArchived = true;
            if (it->second.isASample && this->m_node->GetId() == 0) {
              auto time = Simulator::Now() - it->second.creationTime;
              uint64_t period = time.ToInteger(Time::MS);
              std::cout << period << std::endl;
              it->second.isASample = false;
            }
            continue;
          }
          processed.insert(approvedBlock);
          UpdateWeightAndEntropy(it->second.block, visited, nodeName);
        }
        else {
          NS_LOG_ERROR("it == m_ledger.end(): " << approvedBlock);
          throw 0;
        }
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
}

// Callback that will be called when Data arrives
void
Peer::OnData(std::shared_ptr<const Data> data)
{
  NS_LOG_INFO("OnData(): DATA= " << data->getName().toUri());

  auto dataName = data->getName();
  auto dataNameUri = dataName.toUri();

  bool approvedBlocksInLedger = true;
  bool isTailingRecord = false;

  // Application-level semantics
  auto it = m_ledger.find(dataNameUri);
  if (it != m_ledger.end()){
    return;
  }

  auto it2 = m_missingRecords.find(dataNameUri);
  if (it2 == m_missingRecords.end()) {
    NS_LOG_INFO("Is a Tailing Record");
    isTailingRecord = true;
  }
  else {
    m_missingRecords.erase(it2);
  }

  auto it3 = find(m_blackList.begin(), m_blackList.end(), dataName.get(1).toUri());
  if (it3 != m_blackList.end()) {
    NS_LOG_INFO("Is a record from revoked entity");
    return;
  }

  std::vector<std::string> approvedBlocks = GetApprovedBlocks(data);
  m_recordStack.push_back(LedgerRecord(data));
  for (size_t i = 0; i != approvedBlocks.size(); i++) {
    auto approvedBlockName = Name(approvedBlocks[i]);
    if (approvedBlockName.size() < 2) { // ignoring empty strings when splitting (:tip1:tip2)
      NS_LOG_INFO("IGNORED " << approvedBlockName);
      continue;
    }
    if (approvedBlockName.get(1) == dataName.get(1) && dataName.get(1) != m_idManagerPrefix.get(1)) { // recordname format: /dledger/node/hash
      m_recordStack.pop_back();
      NS_LOG_INFO("INTERLOCK VIOLATION " << approvedBlockName);
      return;
    }
    it = m_ledger.find(approvedBlocks[i]);
    if (it == m_ledger.end()) {
      approvedBlocksInLedger = false;
      it2 = m_missingRecords.find(approvedBlocks[i]);
      if (it2 == m_missingRecords.end()) {
        m_missingRecords.insert(approvedBlocks[i]);
        FetchRecord(approvedBlockName);
        NS_LOG_INFO("GO TO FETCH " << approvedBlockName);
      }
    }
    else {
      NS_LOG_INFO("EXISTS APPROVAL " << approvedBlockName);
      if (isTailingRecord && it->second.entropy > m_conEntropy) {
        NS_LOG_INFO("Break Contribution Policy!!");
        return;
      }
    }
  }

  if (approvedBlocksInLedger) {
    for(auto it = m_recordStack.rbegin(); it != m_recordStack.rend(); ){
      NS_LOG_INFO("STACK SIZE " << m_recordStack.size());

      const auto& record = *it;
      auto recordName = record.block->getName().toUri();
      approvedBlocks = GetApprovedBlocks(record.block);
      bool ready = true;
      for(const auto& approveeName : approvedBlocks){
        if(m_ledger.find(approveeName) == m_ledger.end()){
          ready = false;
          break;
        }
      }
      if(!ready){
        it ++;
        continue;
      }

      NS_LOG_INFO("POPED " << record.block->getName());
      m_tipList.push_back(recordName);
      m_ledger.insert(std::pair<std::string, LedgerRecord>(record.block->getName().toUri(), record));
      if (record.block->getName().getSubName(0, 2) == m_idManagerPrefix) {
        AddRevocation(record.block);
      }

      for (size_t i = 0; i != approvedBlocks.size(); i++) {
        m_tipList.erase(std::remove(m_tipList.begin(),
                                    m_tipList.end(), approvedBlocks[i]), m_tipList.end());

      }
      std::set<std::string> visited;
      UpdateWeightAndEntropy(record.block, visited, record.block->getName().getSubName(0, 2).toUri());
      NS_LOG_INFO("ReceiveRecord: visited records size: " << visited.size()
                  << " unconfirmed depth: " << log2(visited.size() + 1));

      it = decltype(it)(m_recordStack.erase(std::next(it).base()));
    }
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
    auto tipNameStr = tipName.toUri();
    while (tipNameStr != "/") {
      auto it = m_ledger.find(tipNameStr);
      if (it == m_ledger.end()) {
        FetchRecord(tipName);
      }
      else {
        // if weight is greater than 1,
        // this node has more recent tips
        // trigger sync
        if (it->second.weight > 1) {
          std::string syncNameStr(m_mcPrefix.toUri());
          //Name syncName(m_mcPrefix);
          //syncName.append("SYNC");
          syncNameStr += "/SYNC";
          for (size_t i = 0; i != m_tipList.size(); i++) {
            //syncName.append(m_tipList[i]);
            if(syncNameStr[syncNameStr.size() - 1] != '/' && m_tipList[i][0] != '/')
              syncNameStr += "/";
            syncNameStr += m_tipList[i];
          }

          auto syncInterest = std::make_shared<Interest>(syncNameStr);
          m_transmittedInterests(syncInterest, this, m_face);
          m_appLink->onReceiveInterest(*syncInterest);
        }
      }
      iStartComponent += 3;
      tipName = tipDigest.getSubName(iStartComponent, 3);
      tipNameStr = tipName.toUri();
    }
  }
  // else it is record fetching interest
  else {
    auto it = m_ledger.find(interestName.toUri());
    if (it != m_ledger.end()){
      m_appLink->onReceiveData(*it->second.block);
    }
    else {
      // This node doesn't have as well so it tries to fetch
      FetchRecord(interestName);
    }
  }
}

}
}
