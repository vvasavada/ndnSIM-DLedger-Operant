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

namespace ns3 {
namespace ndn {

// register NS-3 Type
TypeId
Peer::GetTypeId()
{
  static TypeId tid = TypeId("Peer")
    .SetParent<App>()
    .AddConstructor<Peer>()
    .AddAttribute("Frequency", "Frequency of record generation", StringValue("1"),
                  MakeIntegerAccessor(&Peer::m_frequency), MakeIntegerChecker<int32_t>())
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
  : m_frequency(1.0)
  , m_firstTime(true)
  , m_weightThreshold(10)
  , m_entropyThreshold(10)
  , m_recordNum(1)
{
}

void
Peer::ScheduleNextGeneration()
{
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
   App::StartApplication();

   // create two hardcoded genesis records and add them to tip list
   Name genesis1Name(m_mcPrefix);
   genesis1Name.append("genesis1");
   auto genesis1 = std::make_shared<Data>(genesis1Name);
   m_tipList.push_back(genesis1Name);
   m_ledger.insert(std::pair<Name, Data>(genesis1Name, *genesis1));

   Name genesis2Name(m_mcPrefix);
   genesis2Name.append("genesis2");
   auto genesis2 = std::make_shared<Data>(genesis2Name);
   m_tipList.push_back(genesis2Name);
   m_ledger.insert(std::pair<Name, Data>(genesis2Name, *genesis2));

   ScheduleNextGeneration();
}

// Processing when application is stopped
void
Peer::StopApplication()
{
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
  auto interestNameUri = interest->getName().toUri();

  // if it is notification interest (/mc-prefix/NOTIF/creator-pref/name)
  if (interestNameUri.find("NOTIF") != std::string::npos) {
    Name recordName(m_mcPrefix);
    recordName.append(interest->getName().getSubName(2).toUri());
    FetchRecord(recordName);

    // else if it is sync interest
  } else if (interestNameUri.find("SYNC") != std::string::npos) {
    //TODO:
      // compare tip list and figure out which tips are not present
      // send fetch record interest to retrieve those tips and missing records recursively
      // if there are some tips that are more recent in local ledger, broadcast sync interest
     
    // else it is record fetching interest
  } else {
    auto it = m_ledger.find(interest->getName());
    if (it != m_ledger.end()){

      m_appLink->onReceiveData(it->second);
    } else {
      //TODO:
      // this node doesnt have, need to retrieve
    }
  }
}

}
}
