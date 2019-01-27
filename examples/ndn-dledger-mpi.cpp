#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/ndnSIM/apps/ndn-peer.hpp"
#include "ns3/mpi-interface.h"
#include <map>
#include <chrono>

#ifdef NS3_MPI
#include <mpi.h>
#else
#error "ndn-simple-mpi scenario can be compiled only if NS3_MPI is enabled"
#endif


using namespace std;
using namespace ns3;

using ns3::ndn::StackHelper;
using ns3::ndn::AppHelper;
using ns3::ndn::L3RateTracer;
using ns3::ndn::FibHelper;
using ns3::ndn::StrategyChoiceHelper;
using ns3::ndn::GlobalRoutingHelper;

NS_LOG_COMPONENT_DEFINE ("ndn.dledger");

void
failLink(Ptr<NetDevice> nd)
{
  Ptr<RateErrorModel> error = CreateObject<RateErrorModel> ();
  error->SetAttribute ("ErrorRate", DoubleValue (1.0));
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
      cout << "\"" << namemap[it.first] << "\"";
      if(it.second.approverNames.size() > 0){
        cout << " -> {";
        for(auto & approver : it.second.approverNames) {
          cout << " \"" << namemap[approver] << "\"";
        }
        cout << " }";
      }
      cout << endl;
    }
    cout << "}" << endl;
  }
  Simulator::Schedule(Seconds(100.0), inspectRecords);
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

  GlobalValue::Bind("SimulatorImplementationType", StringValue("ns3::DistributedSimulatorImpl"));

  // Enable parallel simulator with the command line arguments
  MpiInterface::Enable(&argc, &argv);

  uint32_t systemId = MpiInterface::GetSystemId();
  uint32_t systemCount = MpiInterface::GetSize();

  // Creating nodes
  int node_num = 5;
  NodeContainer nodes;
  for(int i = 0; i < node_num; i ++){
    nodes.Create(1, i % systemCount);
  }

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

    if(counter % systemCount == systemId){
      AppHelper sleepingAppHelper("Peer");
      sleepingAppHelper.SetAttribute("Routable-Prefix", StringValue(prefix));
      sleepingAppHelper.SetAttribute("Multicast-Prefix", StringValue("/dledger"));
      sleepingAppHelper.SetAttribute("Frequency", IntegerValue(1));
      // sleepingAppHelper.SetAttribute("WeightThreshold", IntegerValue(10));
      // sleepingAppHelper.SetAttribute("MaxWeight", IntegerValue(15));
      sleepingAppHelper.SetAttribute("GenesisNum", IntegerValue(5));
      sleepingAppHelper.SetAttribute("ReferredNum", IntegerValue(2));
      sleepingAppHelper.SetAttribute("MaxEntropy", IntegerValue(3));
      sleepingAppHelper.SetAttribute("EntropyThreshold", IntegerValue(2));

      sleepingAppHelper.Install(object).Start(Seconds(2));
    }

    // Add /prefix origins to ndn::GlobalRouter
    ndnGlobalRoutingHelper.AddOrigins(prefix, object);
    ndnGlobalRoutingHelper.AddOrigins("/dledger", object);
    counter++;
  }

  // Calculate and install FIBs
  GlobalRoutingHelper::CalculateRoutes();

  // Finish Installation****************************************************
  // Simulator::Schedule(Seconds(5.0), failLink, nodes.Get(30)->GetDevice(0));
  // Simulator::Schedule(Seconds(5.0), failLink, nodes.Get(31)->GetDevice(0));
  // Simulator::Schedule(Seconds(5.0), failLink, nodes.Get(50)->GetDevice(0));
  // Simulator::Schedule(Seconds(5.0), failLink, nodes.Get(51)->GetDevice(0));
  // Simulator::Schedule(Seconds(20.0), inspectRecords);
  Simulator::Stop(Seconds(100.0));

  auto start_time = std::chrono::steady_clock::now();

  Simulator::Run();

  auto end_time = std::chrono::steady_clock::now();
  std::cout << "ProcessID=" << systemId << " - "
            << std::chrono::duration_cast<std::chrono::duration<double>>(end_time - start_time).count()
            << " secs" << std::endl;

  Simulator::Destroy ();
  MpiInterface::Disable();

  return 0;
}
