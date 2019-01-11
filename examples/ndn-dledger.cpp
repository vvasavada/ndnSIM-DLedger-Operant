#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"
#include <map>

using namespace std;
using namespace ns3;

using ns3::ndn::StackHelper;
using ns3::ndn::AppHelper;
using ns3::ndn::StrategyChoiceHelper;
using ns3::ndn::L3RateTracer;
using ns3::ndn::FibHelper;
using ns3::ndn::StrategyChoiceHelper;

NS_LOG_COMPONENT_DEFINE ("ndn.dledger");

int
main (int argc, char *argv[])
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
  NodeContainer nodes;
  nodes.Create(30);

  // Connecting nodes using two links
  PointToPointHelper p2p;
  p2p.Install(nodes.Get(0), nodes.Get(1));
  p2p.Install(nodes.Get(1), nodes.Get(2));
  for (int i = 0; i < 29; i++) {
    p2p.Install(nodes.Get(i), nodes.Get(i + 1));
  }

  // Install NDN stack on all nodes
  StackHelper ndnHelper;
  ndnHelper.SetDefaultRoutes(true);
  ndnHelper.InstallAll();

  // Finish Preparation****************************************************

  // Choosing forwarding strategy
  StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/multicast");

  // install SyncApp
  int counter = 0;
  for (NodeContainer::Iterator i = nodes.Begin(); i != nodes.End(); ++i) {
    Ptr<Node> object = *i;

    AppHelper sleepingAppHelper("Peer");
    sleepingAppHelper.SetAttribute("Routable-Prefix",
                                   StringValue("/dledger/node" + std::to_string(counter)));
    sleepingAppHelper.SetAttribute("Multicast-Prefix",
                                   StringValue("/dledger"));
    sleepingAppHelper.SetAttribute("Frequency", IntegerValue(1));
    sleepingAppHelper.SetAttribute("WeightThreshold", IntegerValue(10));
    sleepingAppHelper.SetAttribute("MaxWeight", IntegerValue(15));
    sleepingAppHelper.SetAttribute("GenesisNum", IntegerValue(5));
    sleepingAppHelper.SetAttribute("ReferredNum", IntegerValue(2));

    sleepingAppHelper.Install(object).Start(Seconds(2));

    //FibHelper::AddRoute(object, "/dledger/node" + std::to_string(counter) + "/group0",
    //                    std::numeric_limits<int32_t>::max());
    counter++;
  }

  // Finish Installation****************************************************

  Simulator::Stop(Seconds (100.0));

  Simulator::Run();
  Simulator::Destroy ();

  return 0;
}
