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
#include <iomanip>

NS_LOG_COMPONENT_DEFINE ("WifiExperiment1");

using namespace ns3;

uint16_t maxDistance = 100; // m //最大传播距离
uint32_t packetSize  = 1000; // bytes
uint16_t beaconPort  = 80;
uint32_t numNodes    = 210;
uint32_t numPackets  = 1;
Time live_time       = MilliSeconds(10);
double interval      = 12.0; // MilliSeconds schedule可能会产生冲突
bool verbose = false;
uint32_t RXERROR = 0;
uint32_t RXOK = 0;
NodeContainer source_nodes;
struct Neighbor
{
  Ipv4Address m_neighborAddress;
  Mac48Address m_hardwareAddress;
  Time m_expireTime = Simulator::Now();
  uint16_t m_neighborState;
  uint32_t m_id;
  //uint16_t state; //0=F_state, 1=P_state, 2=R_state，用模3运算进行更迭
  bool close;
};

std::map<int, std::vector<Neighbor> > whole_nb;
std::map<int, EventId> whole_event;
std::map<int, int> times;


void Broadcast ( NodeContainer source_nodes, uint32_t sender, Time interPacketInterval, int mark);
void CheckNeighbor( uint32_t nodeId, double conWindow);
static void GenerateTraffic (Ptr<Socket> socket, uint32_t pktSize,
                             uint32_t pktCount, Time pktInterval, int mark )
{
	uint16_t nodeId = socket->GetNode()->GetId();
	std::ostringstream attachinfo;

	if (mark == 1) {
		attachinfo<<"B";
		attachinfo<<nodeId;
		attachinfo<<"@";
	}else if (mark == 2) {
		attachinfo<<"M";
		attachinfo<<nodeId;
		attachinfo<<"@";
		//NS_LOG_UNCOND( "attach neighbor table" << nodeId <<"is");
		for (int k =0; k<whole_nb[nodeId].size();k++) {
			if (whole_nb[nodeId][k].m_neighborState == 2 ) {
				attachinfo<<whole_nb[nodeId][k].m_id;
            	attachinfo<<"|";
					//NS_LOG_UNCOND("id: "<<whole_nb[nodeId][k].m_id << "state: "<< whole_nb[nodeId][k].m_neighborState);
			}
	 	}
		//attachinfo<<"&";
        for (int k =0; k<whole_nb[nodeId].size();k++) {
			if (whole_nb[nodeId][k].m_neighborState != 2 ) {
				attachinfo<<whole_nb[nodeId][k].m_id;
            	attachinfo<<"*";
					//NS_LOG_UNCOND("id: "<<whole_nb[nodeId][k].m_id << "state: "<< whole_nb[nodeId][k].m_neighborState);
			}
	 	}
        attachinfo<<"%";
	}


  if (pktCount > 0)
    {
      //终于写出来了，在这里可以用ostringstream更改发送包的内容！！！！！！
	  std::ostringstream pktContents(attachinfo.str());
      Ptr<Packet> pkt = Create<Packet>((uint8_t *)pktContents.str().c_str(), pktContents.str().length());
	  socket->Send(pkt);

      Simulator::Schedule (Seconds(0), &GenerateTraffic,
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

  InetSocketAddress inetSourceAddr = InetSocketAddress::ConvertFrom (sourceAddress);
  Ipv4Address senderAddr = inetSourceAddr.GetIpv4 ();

  uint8_t *buffer = new uint8_t[receivedPacket->GetSize ()];
  receivedPacket->CopyData(buffer, receivedPacket->GetSize ());
  std::string s = std::string((char*)buffer);

  std::string type=s.substr(0,1);

  uint16_t nodeId = socket->GetNode()->GetId();

  //get the id of sender node
  std::string getSdId = s.substr(1,s.size());
  std::string sdSeq   = "@";
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


	// the type of the packet is Beacon
   if (type=="B") {
	   // 检验所有节点是否已经包含该节点
	   bool flagB = 0;
	   for (int k =0; k<whole_nb[nodeId].size();k++) {
			if (whole_nb[nodeId][k].m_neighborAddress == senderAddr) {
		  	flagB = 1;
		  	whole_nb[nodeId][k].m_expireTime = Simulator::Now() + live_time;
			}
	   }
	   if (flagB == 0) {
		//构建一个中间邻居
		Neighbor rec_nb;
		rec_nb.m_id = sdId;
		rec_nb.m_neighborAddress = senderAddr;
		rec_nb.m_neighborState = 0;
		rec_nb.m_expireTime = Simulator::Now() + live_time;
		whole_nb[nodeId].push_back(rec_nb);
	   }
   }else if (type == "M") {

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

       uint16_t numNeighbors = whole_nb[nodeId].size();
       double conWindow = 0.3*(distance - numNeighbors * maxDistance/10)*(distance - numNeighbors * maxDistance/10)/ (snrValue - 100);
       //NS_LOG_UNCOND (sdId << "distance to " << nodeId <<" : "<< distance <<" Received Packet with SNR =" << snrValue << "contention window: "<< conWindow );
       if (conWindow < 0) {
           conWindow = 1;
       }

	   std::string affiredTable=s.substr(pos,s.size());
	   std::string seg = "|";
	   std::string temp("");
	   int flagM = 0;
	   for (int k =0; k<whole_nb[nodeId].size();k++) {
			if (whole_nb[nodeId][k].m_id == sdId) {
				whole_nb[nodeId][k].m_neighborState = 2;
				whole_nb[nodeId][k].m_expireTime = Simulator::Now() + live_time;
				flagM = 1;
			}
	   }
	   if (nodeId == sdId) {
	   	flagM = 1;
	   }

	   if (flagM == 0) {
		   //std::cout << "add a new sender to neighbor" << '\n';
		   	Neighbor rec_mes;
   			rec_mes.m_id = sdId;
   			rec_mes.m_neighborAddress = senderAddr;
   			rec_mes.m_neighborState = 2;
   			rec_mes.m_expireTime = Simulator::Now() + live_time;
   			whole_nb[nodeId].push_back(rec_mes);
	   }

		for (size_t i = 0; i < affiredTable.size(); i++) {
	   		if (affiredTable[i] == '|') {
	   			//std::cout << "substr: " << temp<< '\n';
				uint32_t attNeighborId = atoi(temp.c_str());
				for (int k =0; k<whole_nb[nodeId].size();k++) {
	 				if (whole_nb[nodeId][k].m_id == attNeighborId) {
                        //std::cout << "substr: " << temp<< '\n';
	 					whole_nb[nodeId][k].m_neighborState = 2;
	 				}
	 	   		}
				temp = "";
	   		}else if (affiredTable[i] == '%') {
	   			break;
	   		}else if (affiredTable[i] == '*') {
	   		    uint32_t attNeighborId = atoi(temp.c_str());

                for (int k =0; k<whole_nb[nodeId].size();k++) {
	 				if (whole_nb[nodeId][k].m_id == attNeighborId || nodeId == attNeighborId) {
                        EventId tranMessage = Simulator::Schedule (MilliSeconds(0), &Broadcast,
                                           source_nodes, nodeId, MilliSeconds(interval),2);
                        whole_event[nodeId] = tranMessage;
	 				}
	 	   		}
				temp = "";
	   		}else{
				temp+=affiredTable[i];
			}
		}
        Simulator::Schedule(MilliSeconds(0), &CheckNeighbor, nodeId,conWindow);

   }
}

void CheckNeighbor( uint32_t nodeId, double conWindow) {

    int countNState = 0;

    for (int k =0; k<whole_nb[nodeId].size();k++) {
        if (whole_nb[nodeId][k].m_neighborState != 2) {

            //std::cout << nodeId << "broadcast message, conWindow is " << conWindow <<'\n';
            EventId tranMessage = Simulator::Schedule (MilliSeconds(conWindow), &Broadcast,
                               source_nodes, nodeId, MilliSeconds(interval),2);
            whole_event[nodeId] = tranMessage;
            break;
        }else{
            countNState +=1;
            EventId bMessage = whole_event[nodeId];
                if (bMessage.IsRunning()) {
                    Simulator::Cancel(bMessage);
                }
                Simulator::Remove(bMessage);
        }
    }
     if (countNState != whole_nb[nodeId].size()) {
        //std::cout << nodeId << " times:  " << times[nodeId] << '\n';
        if (times[nodeId]<=4) {
            Simulator::Schedule(MilliSeconds(5), &CheckNeighbor, nodeId,conWindow);
            times[nodeId] +=1;
        }
    }else{
        // for (int k =0; k<whole_nb[nodeId].size();k++) {
        //         //std::cout <<"node " << nodeId << " finish the broadcast processing " << '\n';
        //        whole_nb[nodeId][k].m_neighborState = 0;
        // }
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
     if (mark == 2) {
         //NS_LOG_UNCOND(sender << "Broadcast message");
         for (int k =0; k<whole_nb[sender].size();k++) {
             if (whole_nb[sender][k].m_neighborState == 0) {
                whole_nb[sender][k].m_neighborState = 1;
             }
         }
     }


     //消除所有超时节点
          for(std::vector<Neighbor>::iterator iter = whole_nb[sender].begin(); iter != whole_nb[sender].end(); )
          {
              //std::cout << nodeId << "expireTime: " << iter->m_expireTime.GetMilliSeconds() << " now ::" << Simulator::Now().GetMilliSeconds()<< '\n';
              if(iter->m_expireTime < Simulator::Now())
              {
                  whole_nb[sender].erase(iter);
              }
              else
              {
                  ++iter;
              }
          }

	 Simulator::ScheduleWithContext(source->GetNode ()->GetId (), MilliSeconds(0), &GenerateTraffic,
								   source, packetSize, numPackets, MilliSeconds(0), mark);

        if (mark == 1) {
            Simulator::Schedule (interPacketInterval, &Broadcast,
            			 source_nodes, sender, interPacketInterval, 1);
        }
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
  // LogComponentEnable("PropagationLossModel",LOG_DEBUG);
  // LogComponentEnable ("YansWifiPhy", LOG_LEVEL_DEBUG);
  std::string phyMode ("DsssRate1Mbps");
  std::string traceFile;


  //bool tracing = false;

  CommandLine cmd;

  cmd.AddValue ("phyMode", "Wifi Phy mode", phyMode);
  cmd.AddValue ("packetSize", "size of application packet sent", packetSize);
  cmd.AddValue ("numPackets", "number of packets generated", numPackets);
  cmd.AddValue ("interval", "interval (MilliSeconds) between packets", interval);
  cmd.AddValue ("verbose", "turn on all WifiNetDevice log components", verbose);
  cmd.AddValue ("beaconPort", "port number to send beacons on", beaconPort);
  cmd.AddValue ("traceFile", "Ns2 movement trace file", traceFile);

  cmd.Parse (argc, argv);
  // Convert to time object
  Time interPacketInterval = MilliSeconds (interval);

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
  wifiPhy.Set ("RxNoiseFigure", DoubleValue (10));

  Config::SetDefault( "ns3::RangePropagationLossModel::MaxRange", DoubleValue( maxDistance ) );
  YansWifiChannelHelper wifiChannel;
  wifiChannel.SetPropagationDelay( "ns3::ConstantSpeedPropagationDelayModel" );
  wifiChannel.AddPropagationLoss("ns3::NakagamiPropagationLossModel");
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
    //     mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
       mobility.SetMobilityModel ("ns3::RandomDirection2dMobilityModel",
                                   "Bounds", RectangleValue (Rectangle (0, 500,0,100)),
                                   "Speed", StringValue ("ns3::ConstantRandomVariable[Constant=10.0]"),
                                   "Pause", StringValue ("ns3::ConstantRandomVariable[Constant=0.2]"));

        mobility.Install (source_nodes);
  }else{
      Ns2MobilityHelper mobility = Ns2MobilityHelper (traceFile);
      mobility.Install ();
  }

  Config::Connect("/NodeList/*/DeviceList/*/Phy/State/RxOk",MakeCallback(&PhyRxOkTrace));
  Config::Connect("/NodeList/*/DeviceList/*/Phy/State/RxError",MakeCallback(&PhyRxErrorTrace));

   InternetStackHelper internet;
   internet.Install (source_nodes);

  Ipv4AddressHelper ipv4_source;
  NS_LOG_INFO ("Assign IP Addresses.");
  ipv4_source.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i_source = ipv4_source.Assign (source_devices);


  for (size_t i = 0; i < numNodes; i++) {
  	Simulator::Schedule (MilliSeconds(i%10), &Broadcast,
  				source_nodes, i, MilliSeconds(interval), 1);
  }
  uint32_t sender = 0;

  EventId bMessage = Simulator::Schedule (MilliSeconds(20), &Broadcast,
    				   source_nodes, sender, MilliSeconds(interval),2);
  whole_event[sender] = bMessage;


  // Tracing
  //wifiPhy.EnablePcap ("wifi-experiment1", source_devices);
  AnimationInterface anim("beacon1.xml");

  Simulator::Stop(MilliSeconds(70));
  Simulator::Run ();

  NS_LOG_UNCOND( "neighbor table: ");
  int recount = 0;
  for ( int i =0; i< whole_nb.size(); i++) {
      int reflag = 0;
      std::cout.width(5);
      std::cout << i  << ": ";
	for (int k =0; k<whole_nb[i].size();k++) {
        std::cout.width(5);
        if (whole_nb[i][k].m_neighborState != 0) {
            reflag = 1;
        }
        std::cout << whole_nb[i][k].m_id <<"("<< whole_nb[i][k].m_neighborState << ")    ";
	}
    if (reflag == 1) {
        recount +=1;
    }
    std::cout  << '\n';
  }
  double p =(RXERROR + RXOK/2);

  std::cout << "total number : " << recount  << " Prob: "<<RXERROR/p<< '\n';

  Simulator::Destroy ();

  return 0;
}
