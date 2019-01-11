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

#include "../ndn-cxx/src/encoding/block-helpers.hpp"
#include "../ndn-cxx/src/encoding/tlv.hpp"
#include "ns3/ndnSIM/helper/ndn-stack-helper.hpp"

NS_LOG_COMPONENT_DEFINE("ndn.peer");

namespace ns3 {
namespace ndn {

NS_OBJECT_ENSURE_REGISTERED(Peer);

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
    .AddAttribute("WeightThreshold", "Weight to consider archive", IntegerValue(10),
                  MakeIntegerAccessor(&Peer::m_weightThreshold), MakeIntegerChecker<int32_t>())
    .AddAttribute("MaxWeight", "The max weight a block can gain", IntegerValue(15),
                  MakeIntegerAccessor(&Peer::m_maxWeight), MakeIntegerChecker<int32_t>())
    .AddAttribute("EntropyThreshold", "Entropy value of peers", IntegerValue(5),
                  MakeIntegerAccessor(&Peer::m_entropyThreshold), MakeIntegerChecker<int32_t>())
    .AddAttribute("GenesisNum", "Number of genesis blocks", IntegerValue(5),
                  MakeIntegerAccessor(&Peer::m_genesisNum), MakeIntegerChecker<int32_t>())
    .AddAttribute("ReferredNum", "Number of referred blocks", IntegerValue(2),
                  MakeIntegerAccessor(&Peer::m_referredNum), MakeIntegerChecker<int32_t>())
    //********
    .AddAttribute("Routable-Prefix", "Node's Prefix, for which producer has the data", StringValue("/"),
                  MakeNameAccessor(&Peer::m_routablePrefix), MakeNameChecker())
    .AddAttribute("Multicast-Prefix", "Multicast Prefix", StringValue("/"),
                  MakeNameAccessor(&Peer::m_mcPrefix), MakeNameChecker())
    .AddAttribute("Randomize",
                  "Type of send time randomization: none (default), uniform, exponential",
                  StringValue("none"),
                  MakeStringAccessor(&Peer::SetRandomize, &Peer::GetRandomize),
                  MakeStringChecker());
  return tid;
}

Peer::Peer()
  : m_firstTime(true)
  , m_recordNum(1)
{
}

void
Peer::ScheduleNextGeneration()
{
  NS_LOG_FUNCTION_NOARGS();
  // double mean = 8.0 * m_payloadSize / m_desiredRate.GetBitRate ();
  // std::cout << "next: " << Simulator::Now().ToDouble(Time::S) + mean << "s\n";

  if (m_firstTime) {
    m_sendEvent = Simulator::Schedule(Seconds(0.0), &Peer::GenerateRecord, this);
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
  //TODO
}

void
Peer::SetRandomize(const std::string& value)
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

  m_randomType = value;
}

std::string
Peer::GetRandomize() const
{
  return m_randomType;
}

// Processing upon start of the application
void
Peer::StartApplication()
{
  NS_LOG_FUNCTION_NOARGS();
  App::StartApplication();

  // create genesis blocks in the DLedger
  for (int i = 0; i < m_genesisNum; i++) {
    Name genesisName(m_mcPrefix);
    genesisName.append("genesis" + std::to_string(i));
    auto genesis = std::make_shared<Data>(genesisName);
    m_tipList.push_back(genesisName);
    m_ledger.insert(std::pair<Name, Data>(genesisName, *genesis));
  }

  ScheduleNextGeneration();
}

// Processing when application is stopped
void
Peer::StopApplication()
{
  NS_LOG_FUNCTION_NOARGS();
  // cleanup App
  App::StopApplication();
}

// Generate a new record and send out notif and sync interest
void
Peer::GenerateRecord()
{
  // generate a new record
  Name recordName(m_mcPrefix);
  recordName.append(m_routablePrefix.toUri()).append(std::to_string(m_recordNum));
  auto record = std::make_shared<Data>(recordName);

  //TODO: need to add while loop for below code and
  // keep selecting references until you get
  // both of them produced by different node
  // i.e. name shouldn't contain this node's routable prefix

  // select two references randomly
  auto reference1Index = rand() % (m_tipList.size() - 1);
  auto reference2Index = rand() % (m_tipList.size() - 1);
  auto reference1 = m_tipList.at(reference1Index);
  auto reference2 = m_tipList.at(reference2Index);

  // if indices are not same, we got two different references
  // remove both of them from tiplist
  // else
  // remove the tip that got selected twice
  if (reference1Index != reference2Index){
    m_tipList.erase(m_tipList.begin() + reference1Index);
    m_tipList.erase(m_tipList.begin() + reference2Index);
  } else {
    m_tipList.erase(m_tipList.begin() + reference1Index);
  }

  record->setContent(::ndn::encoding::makeStringBlock(::ndn::tlv::Content,
                                                      reference1.toUri() + ":" + reference2.toUri()));
  ndn::StackHelper::getKeyChain().sign(*record);

  // attach to local ledger
  m_ledger.insert(std::pair<Name, Data>(recordName, *record));

  // add to tip list
  m_tipList.push_back(recordName);

  //TODO: increment weight (need recursive function)

  Name notifName(m_mcPrefix);
  notifName.append("NOTIF").append(m_routablePrefix.toUri()).append(std::to_string(m_recordNum));
  auto notif = std::make_shared<Interest>(notifName);
  m_transmittedInterests(notif, this, m_face);
  m_appLink->onReceiveInterest(*notif);

  m_recordNum++;

  ScheduleNextGeneration();
}


// Send out interest to fetch record
void
Peer::FetchRecord(Name recordName)
{
  auto recordInterest = std::make_shared<Interest>(recordName);
  m_transmittedInterests(recordInterest, this, m_face);
  m_appLink->onReceiveInterest(*recordInterest);
}

// Callback that will be called when Data arrives
void
Peer::OnData(std::shared_ptr<const Data> data)
{
  NS_LOG_FUNCTION(this << data);
  //TODO:
  // ignore data if it is just reply to notif and sync interest
  // if data is a record:
  // Verification:
  // verify signature (PoA)
  // verify application level semantics (record is not already in ledger)
  // verify record does refer to records generated by same producer
  // ...
  // When verification passes, check if approved records are in local ledger
  // if not, retrieve them recursively and keep queuing
  // once all records are obtained, remove from queue and add to local ledger
  // (note if one of the records during sync fails to receive, discard entire queue)
  // increment weights of direct and indirect referred records
  // archive records that passes weight and entropy thresholds
}

// Callback that will be called when Interest arrives
void
Peer::OnInterest(std::shared_ptr<const Interest> interest)
{
  NS_LOG_FUNCTION(this << interest);
  auto interestName = interest->getName();
  auto interestNameUri = interestName.toUri();

  // if it is notification interest (/mc-prefix/NOTIF/creator-pref/name)
  if (interestNameUri.find("NOTIF") != std::string::npos) {
    Name recordName(m_mcPrefix);
    recordName.append(interestName.getSubName(2).toUri());
    FetchRecord(recordName);

    // else if it is sync interest (/mc-prefix/SYNC/tip1/tip2 ...)
    // note that here tip1 will be /mc-prefix/creator-pref/name)
  }
  else if (interestNameUri.find("SYNC") != std::string::npos) {
    auto tipDigest = interestName.getSubName(2);
    int iStartComponent = 0;
    auto tipName = tipDigest.getSubName(iStartComponent, iStartComponent + 2);
    while (tipName.toUri() != "/") {
      auto it = m_ledger.find(tipName);
      if (it == m_ledger.end()) {
        FetchRecord(tipName);
      } else {
        // if weight is greater than 1,
        // this node has more recent tips
        // trigger sync
        auto it = m_weightList.find(tipName);
        if (it != m_weightList.end()) { // this should ALWAYS return true
          if (it->second > 1) {
            Name syncName(m_mcPrefix);
            syncName.append("SYNC");
            for (auto i = 0; i != m_tipList.size(); i++) {
              syncName.append(m_tipList[i].toUri());
            }

            auto syncInterest = std::make_shared<Interest>(syncName);
            m_transmittedInterests(syncInterest, this, m_face);
            m_appLink->onReceiveInterest(*syncInterest);
          }
        }
      }
      iStartComponent += 3;
      tipName = tipDigest.getSubName(iStartComponent, iStartComponent + 2);
    }

    // else it is record fetching interest
  }
  else {
    auto it = m_ledger.find(interestName);
    if (it != m_ledger.end()){
      m_appLink->onReceiveData(it->second);
    }
    else {
      //TODO:
      // this node doesnt have, need to retrieve
    }
  }
}

}
}
