#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/ndnSIM/apps/ndn-peer.hpp"
#include <map>
#include <chrono>

using namespace std;
using namespace ns3;

using ns3::ndn::StackHelper;
using ns3::ndn::AppHelper;
using ns3::ndn::L3RateTracer;
using ns3::ndn::FibHelper;
using ns3::ndn::StrategyChoiceHelper;
using ns3::ndn::GlobalRoutingHelper;

NS_LOG_COMPONENT_DEFINE ("ndn.dledger");

const int NodesCnt = 15;
const int EntropyThreshold = 8;
const int MaxEntropy = 5;
const double TotalTime = 100.0;

void
failLink(Ptr<NetDevice> nd)
{
  Ptr<RateErrorModel> error = CreateObject<RateErrorModel> ();
  error->SetAttribute ("ErrorRate", DoubleValue (1.0));
  nd->SetAttribute ("ReceiveErrorModel", PointerValue(error));
}

void
upLink(Ptr<NetDevice> nd)
{
  Ptr<RateErrorModel> error = CreateObject<RateErrorModel> ();
  error->SetAttribute ("ErrorRate", DoubleValue (-0.1));
  nd->SetAttribute ("ReceiveErrorModel", PointerValue(error));
}

// TODO: THE RESULTS INTERWAVES; USE FILE TO OUTPUT.
void
inspectRecords()
{
  map<std::string, std::string> namemap;

  for(auto node = NodeList::Begin(); node != NodeList::End(); ++ node) {
    if((*node)->GetNApplications() == 0)
      continue;
    auto peer = DynamicCast<ns3::ndn::Peer>((*node)->GetApplication(0));
    auto & ledger = peer->GetLedger();
    cout << "================================================" << endl;
    cout << "TIME: " << Simulator::Now() << endl;
    cout << "Node Id: " << (*node)->GetId() << " Ledger Size: " << ledger.size() << endl;
    cout << "digraph{" << endl;

    namemap.clear();
    int cnt = 0;
    for(auto & it : ledger){
      namemap[it.first] = it.first.substr(9, 16);
    }

    for(auto & it : ledger) {
      auto approvees = peer->GetApprovedBlocks(it.second.block);

      cout << "\"" << namemap[it.first] << "\"";

      if(approvees.size() > 0){
        cout << " -> {";
        for(const auto & approvee : approvees){
          cout << " \"" << namemap[approvee] << "\"";
        }
        cout << " }";
      }


      // if(it.second.approverNames.size() > 0){
      //   cout << " -> {";
      //   for(auto & approver : it.second.approverNames) {
      //     cout << " \"" << namemap[approver] << "\"";
      //   }
      //   cout << " }";
      // }
      cout << endl;
    }
    cout << "}" << endl;

    //break;
  }
  Simulator::Schedule(Seconds(100.0), inspectRecords);
}

std::chrono::steady_clock::time_point start_time;

void
showProgress(){
  auto end_time = std::chrono::steady_clock::now();
  static int progress = 0;
  std::cout << ++ progress << "% "
            << std::chrono::duration_cast<std::chrono::duration<double>>(end_time - start_time).count()
            << "sec";
            //<< std::endl;

  auto node = NodeList::Begin();
  for(; node != NodeList::End(); ++ node) {
    if((*node)->GetNApplications() == 0)
      continue;
    break;
  }

  auto peer = DynamicCast<ns3::ndn::Peer>((*node)->GetApplication(0));
  auto & ledger = peer->GetLedger();
  int unconfirmedCnt = 0;
  for(const auto & record : ledger){
    if(record.second.entropy < EntropyThreshold) { // EntropyThreshold -> confirm
      unconfirmedCnt ++;
    }
  }
  std::cout << " Total Count=" << ledger.size();
  std::cout << " Unconfirmed Count=" << unconfirmedCnt;
  std::cout << std::endl;

  Simulator::Schedule(Seconds(1.0), showProgress);
}

int
main(int argc, char *argv[])
{
  // setting default parameters for PointToPoint links and channels
  Config::SetDefault("ns3::PointToPointNetDevice::DataRate", StringValue("10Mbps"));
  Config::SetDefault("ns3::PointToPointChannel::Delay", StringValue("10ms"));
  Config::SetDefault("ns3::QueueBase::MaxSize",
                     QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS, 20)));

  // Read optional command-line parameters (e.g., enable visualizer with ./waf --run=<> --visualize
  CommandLine cmd;
  cmd.Parse(argc, argv);

  // Creating nodes
  int node_num = NodesCnt;
  NodeContainer nodes;
  nodes.Create(node_num);

  // Connecting nodes using two links
  PointToPointHelper p2p;
  for (int i = 0; i < node_num - 1; i++) {
    p2p.Install(nodes.Get(i), nodes.Get(i + 1));
  }

  // Install NDN stack on all nodes
  StackHelper ndnHelper;
  ndnHelper.SetDefaultRoutes(true);
  ndnHelper.InstallAll();

  // Finish Preparation****************************************************

  // Choosing forwarding strategy
  StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/multicast");

  // Installing global routing interface on all nodes
  GlobalRoutingHelper ndnGlobalRoutingHelper;
  ndnGlobalRoutingHelper.InstallAll();

  // install SyncApp
  int counter = 0;
  for (NodeContainer::Iterator i = nodes.Begin(); i != nodes.End(); ++i) {
    Ptr<Node> object = *i;
    std::string prefix = "/dledger/node" + std::to_string(counter);

    AppHelper sleepingAppHelper("Peer");
    sleepingAppHelper.SetAttribute("Routable-Prefix", StringValue(prefix));
    sleepingAppHelper.SetAttribute("Multicast-Prefix", StringValue("/dledger"));
    sleepingAppHelper.SetAttribute("Frequency", DoubleValue(0.2));
    sleepingAppHelper.SetAttribute("SyncFrequency", DoubleValue(0.1));
    sleepingAppHelper.SetAttribute("GenesisNum", IntegerValue(5));
    sleepingAppHelper.SetAttribute("ReferredNum", IntegerValue(2));
    sleepingAppHelper.SetAttribute("ConEntropy", IntegerValue(EntropyThreshold - 3));
    sleepingAppHelper.SetAttribute("EntropyThreshold", IntegerValue(EntropyThreshold));

    sleepingAppHelper.Install(object).Start(Seconds(2));

    // Add /prefix origins to ndn::GlobalRouter
    ndnGlobalRoutingHelper.AddOrigins(prefix, object);
    ndnGlobalRoutingHelper.AddOrigins("/dledger", object);
    counter++;
  }

  // Calculate and install FIBs
  GlobalRoutingHelper::CalculateRoutes();

  // Finish Installation****************************************************
  Simulator::Schedule(Seconds(20.0), failLink, nodes.Get(7)->GetDevice(0));
  Simulator::Schedule(Seconds(80.0), upLink, nodes.Get(7)->GetDevice(0));

  Simulator::Schedule(Seconds(TotalTime - 0.1), inspectRecords);
  Simulator::Stop(Seconds(TotalTime));

  start_time = std::chrono::steady_clock::now();

  Simulator::Run();
  Simulator::Destroy ();
  return 0;
}
