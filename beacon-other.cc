#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/config-store-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/netanim-module.h"
#include "ns3/ipv4-header.h"
#include "ns3/packet.h"
#include "ns3/tag.h"
#include "ns3/on-off-helper.h"
#include "ns3/snr-tag.h"
#include "ns3/event-id.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <map>

NS_LOG_COMPONENT_DEFINE ("WifiExperiment1");

using namespace ns3;

uint16_t maxDistance = 100; // m //最大传播距离
uint32_t packetSize  = 1000; // bytes
uint16_t beaconPort  = 80;
uint32_t numNodes    = 200;
uint32_t numPackets  = 1;
Time live_time       = Seconds(10);
bool verbose         = false;
uint32_t RXERROR     = 0;
uint32_t RXOK        = 0;
double interval      = 10.0; // seconds schedule可能会产生冲突
NodeContainer source_nodes;

std::map<int, EventId> whole_event;
std::map<int, int> state;

void Broadcast ( NodeContainer source_nodes, uint32_t sender, Time interPacketInterval, int mark);

static void GenerateTraffic (Ptr<Socket> socket, uint32_t pktSize,
                             uint32_t pktCount, Time pktInterval, int mark )
{
	uint16_t nodeId = socket->GetNode()->GetId();
	std::ostringstream attachinfo;
    attachinfo<<"M";
    attachinfo<<nodeId;
    attachinfo<<"@";


  if (pktCount > 0)
    {
      //终于写出来了，在这里可以用ostringstream更改发送包的内容！！！！！！
	  std::ostringstream pktContents(attachinfo.str());
      Ptr<Packet> pkt = Create<Packet>((uint8_t *)pktContents.str().c_str(), pktContents.str().length());
	  socket->Send(pkt);

      Simulator::Schedule (pktInterval, &GenerateTraffic,
                           socket, pktSize,pktCount-1, pktInterval,mark);
    }
  else
    {
      socket->Close ();
    }
}


void ReceivePacket (Ptr<Socket> socket)
{


  Ptr<Packet> receivedPacket;
  Address sourceAddress;
  receivedPacket = socket->RecvFrom (sourceAddress);


  uint8_t *buffer = new uint8_t[receivedPacket->GetSize ()];
  receivedPacket->CopyData(buffer, receivedPacket->GetSize ());
  std::string s = std::string((char*)buffer);

  std::string type=s.substr(0,1);

  uint16_t nodeId = socket->GetNode()->GetId();

  //get the id of sender node
  std::string getSdId = s.substr(1,s.size());
  std::string sdSeq = "@";
  std::string sdString("");
  int pos = 1;
  for (size_t i = 0; i < getSdId.size(); i++) {
	  pos +=1;
   if (getSdId[i]=='@') {
	   break;
   }else{
	   sdString+=getSdId[i];
   }
  }
  uint32_t sdId = atoi(sdString.c_str());
   //



       // NS_LOG_UNCOND ( "Received one packet!" << nodeId << " to " <<
       // senderAddr<<" uid: " << receivedPacket->GetUid() << " senderContext : " << s << " type: " << type << "senderId: " << sdId );

       uint16_t snrValue = 0;

       //calculate the contention window
       SnrTag tag;
       if (receivedPacket->PeekPacketTag(tag))
       {
         snrValue = tag.Get();
       }
       Ptr<MobilityModel> model1 = source_nodes.Get(sdId)->GetObject<MobilityModel>();
       Ptr<MobilityModel> model2 = source_nodes.Get(nodeId)->GetObject<MobilityModel>();
       double distance = model1->GetDistanceFrom (model2);


       double conWindow = 0.3*(distance -  maxDistance)*(distance - maxDistance)/ (snrValue - 100);
       //NS_LOG_UNCOND (sdId << "distance to " << nodeId <<" : "<< distance <<" Received Packet with SNR =" << snrValue << "contention window: "<< conWindow );
       if (conWindow < 0) {
           conWindow = 1;
       }

       if (state[nodeId] == 0) {
           EventId tranMessage = Simulator::Schedule (Seconds(conWindow), &Broadcast,
                              source_nodes, nodeId, Seconds(interval),2);
           whole_event[nodeId] = tranMessage;
           state[nodeId] = 1;
           return;
       }else if (state[nodeId] == 1) {
          state[nodeId] = 2;
          EventId tranMessage = Simulator::Schedule (Seconds(conWindow), &Broadcast,
                             source_nodes, nodeId, Seconds(interval),2);
          whole_event[nodeId] = tranMessage;

          return;
       }else if (state[nodeId] == 2) {
           EventId bMessage = whole_event[nodeId];
               if (bMessage.IsRunning()) {
                   Simulator::Cancel(bMessage);
               }
               Simulator::Remove(bMessage);
               return;
       }


}

void Broadcast ( NodeContainer source_nodes, uint32_t sender, Time interPacketInterval, int mark)
{


	TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");

	Ptr<Socket> source = Socket::CreateSocket (source_nodes.Get (sender), tid);
	InetSocketAddress remote = InetSocketAddress (Ipv4Address ("255.255.255.255"), beaconPort);
	source->SetAllowBroadcast (true);
	source->Connect (remote);

	for (int n = 0; n < (int)numNodes; n++){

		Ptr<Socket> beacon_sink = Socket::CreateSocket(source_nodes.Get(n),tid);
	    InetSocketAddress beacon_local = InetSocketAddress(Ipv4Address::GetAny(), beaconPort);

	    beacon_sink->Bind(beacon_local);
	    beacon_sink->SetRecvCallback(MakeCallback(&ReceivePacket));

     }

	 Simulator::ScheduleWithContext(source->GetNode ()->GetId (), Seconds(0), &GenerateTraffic,
								   source, packetSize, numPackets, Seconds(0), mark);


}

void
PhyRxOkTrace (std::string context, Ptr<const Packet> packet, double
snr, WifiMode mode, enum WifiPreamble preamble)
{
	RXOK +=1;
	if (verbose) {
		std::cout << "PHYRXOK mode=" << mode << " snr=" << snr << " " <<
  *packet << std::endl;
	}


}

void
PhyRxErrorTrace (std::string context, Ptr<const Packet> packet, double
snr)
{
	RXERROR +=1;
	if (verbose) {
		std::cout << "PHYRXERROR snr=" << snr << " " << *packet <<
  std::endl;
	}
}

int main (int argc, char *argv[])
{
    //LogComponentEnable("PropagationLossModel",LOG_DEBUG);
  std::string phyMode ("DsssRate1Mbps");
  std::string traceFile;


  //bool tracing = false;

  CommandLine cmd;

  cmd.AddValue ("phyMode", "Wifi Phy mode", phyMode);
  cmd.AddValue ("packetSize", "size of application packet sent", packetSize);
  cmd.AddValue ("numPackets", "number of packets generated", numPackets);
  cmd.AddValue ("interval", "interval (seconds) between packets", interval);
  cmd.AddValue ("verbose", "turn on all WifiNetDevice log components", verbose);
  cmd.AddValue ("beaconPort", "port number to send beacons on", beaconPort);
  cmd.AddValue ("traceFile", "Ns2 movement trace file", traceFile);

  cmd.Parse (argc, argv);
  // Convert to time object
  Time interPacketInterval = Seconds (interval);

  // disable fragmentation for frames below 2200 bytes
  Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("2200"));
  // turn off RTS/CTS for frames below 2200 bytes
  Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("2200"));
  // Fix non-unicast data rate to be the same as that of unicast
  Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode",
                      StringValue (phyMode));

  //NodeContainer source_nodes;
  source_nodes.Create (numNodes);

  // The below set of helpers will help us to put together the wifi NICs we want
  WifiHelper wifi;
  if (verbose)
    {
      wifi.EnableLogComponents ();  // Turn on all Wifi logging
    }
  wifi.SetStandard (WIFI_PHY_STANDARD_80211b);

  YansWifiPhyHelper wifiPhy =  YansWifiPhyHelper::Default ();
  // This is one parameter that matters when using FixedRssLossModel
  // set it to zero; otherwise, gain will be added
  // wifiPhy.Set ("RxGain", DoubleValue (0) );
  // // ns-3 supports RadioTap and Prism tracing extensions for 802.11b
  // wifiPhy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11_RADIO);
  wifiPhy.Set("TxPowerStart", DoubleValue(43));
  wifiPhy.Set("TxPowerEnd", DoubleValue(43));
  wifiPhy.Set("TxPowerLevels", UintegerValue(1));
  wifiPhy.Set("TxGain", DoubleValue(1));
  wifiPhy.Set("RxGain", DoubleValue (1));
  wifiPhy.Set ("RxNoiseFigure", DoubleValue (5));

  Config::SetDefault( "ns3::RangePropagationLossModel::MaxRange", DoubleValue( maxDistance ) );
  YansWifiChannelHelper wifiChannel;
  wifiChannel.SetPropagationDelay( "ns3::ConstantSpeedPropagationDelayModel" );
  wifiChannel.AddPropagationLoss("ns3::NakagamiPropagationLossModel","Distance1",DoubleValue(50),"Distance2",DoubleValue(150),"m0",DoubleValue(3),"m1",DoubleValue(1.5),"m2",DoubleValue(1));
  wifiChannel.AddPropagationLoss(  "ns3::RangePropagationLossModel" );
  wifiPhy.SetChannel( wifiChannel.Create() );
  wifiPhy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11);
  // Add a non-QoS upper mac, and disable rate control
  NqosWifiMacHelper wifiMac = NqosWifiMacHelper::Default ();
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode",StringValue (phyMode),
                                "ControlMode",StringValue (phyMode));
  // Set it to adhoc mode
  wifiMac.SetType ("ns3::AdhocWifiMac");
  NetDeviceContainer source_devices = wifi.Install (wifiPhy, wifiMac, source_nodes);

  // Note that with FixedRssLossModel, the positions below are not
  // used for received signal strength.

  // For now let's use 5x5 grid
  if (traceFile.empty()) {
      MobilityHelper mobility;
      mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
                                      "MinX", DoubleValue (0.0),
                                      "MinY", DoubleValue (0.0),
                                      "DeltaX", DoubleValue (25),
                                      "DeltaY", DoubleValue (25),
                                      "GridWidth", UintegerValue (20),
                                      "LayoutType", StringValue ("RowFirst"));
        //mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
       mobility.SetMobilityModel ("ns3::RandomDirection2dMobilityModel",
                                   "Bounds", RectangleValue (Rectangle (0, 500,0,100)),
                                   "Speed", StringValue ("ns3::ConstantRandomVariable[Constant=10.0]"),
                                   "Pause", StringValue ("ns3::ConstantRandomVariable[Constant=0.2]"));

        mobility.Install (source_nodes);
  }else{
      Ns2MobilityHelper mobility = Ns2MobilityHelper (traceFile);
      mobility.Install ();
  }



   InternetStackHelper internet;
   internet.Install (source_nodes);

  Ipv4AddressHelper ipv4_source;
  NS_LOG_INFO ("Assign IP Addresses.");
  ipv4_source.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i_source = ipv4_source.Assign (source_devices);


  uint32_t sender = 0;
  EventId bMessage = Simulator::Schedule (MilliSeconds(15), &Broadcast,
    				   source_nodes, sender, Seconds(interval),2);
  whole_event[sender] = bMessage;

  Config::Connect("/NodeList/*/DeviceList/*/Phy/State/RxOk",MakeCallback(&PhyRxOkTrace));
  Config::Connect("/NodeList/*/DeviceList/*/Phy/State/RxError",MakeCallback(&PhyRxErrorTrace));

  // Tracing
  wifiPhy.EnablePcap ("wifi-experiment1", source_devices);
  AnimationInterface anim("beacon-other.xml");

  Simulator::Stop(MilliSeconds(70));
  Simulator::Run ();
  int recount = 0;
  for ( int i =0; i< state.size(); i++) {
      std::cout.width(5);
      std::cout << i  << ": ";

        std::cout <<state[i] ;
        if (state[i]==2) {
            recount +=1;
        }
    std::cout  << '\n';
  }
  double p = (RXERROR + RXOK/2);
   std::cout << "total number : " << recount << "asd" << RXERROR <<" asd" << RXOK <<" Prob: " << RXERROR/p << '\n';
  Simulator::Destroy ();

  return 0;
}
