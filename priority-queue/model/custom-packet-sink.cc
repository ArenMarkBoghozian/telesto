/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright 2007 University of Washington
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author:  Tom Henderson (tomhend@u.washington.edu)
 */
#include "ns3/address.h"
#include "ns3/address-utils.h"
#include "ns3/log.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/node.h"
#include "ns3/socket.h"
#include "ns3/udp-socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "custom-packet-sink.h"

#include "ns3/uinteger.h"
#include "ns3/string.h"
#include <fstream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("CustomPacketSink");
NS_OBJECT_ENSURE_REGISTERED (CustomPacketSink);

TypeId 
CustomPacketSink::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::CustomPacketSink")
    .SetParent<Application> ()
    .AddConstructor<CustomPacketSink> ()
    .AddAttribute ("Local", "The Address on which to Bind the rx socket.",
                   AddressValue (),
                   MakeAddressAccessor (&CustomPacketSink::m_local),
                   MakeAddressChecker ())
    .AddAttribute ("Protocol", "The type id of the protocol to use for the rx socket.",
                   TypeIdValue (UdpSocketFactory::GetTypeId ()),
                   MakeTypeIdAccessor (&CustomPacketSink::m_tid),
                   MakeTypeIdChecker ())

	.AddAttribute("TotalExpectedRx",
				  "The total expected to-be-received bytes .",
				  UintegerValue(1),
				  MakeUintegerAccessor(&CustomPacketSink::m_totalExpectedRx),
				  MakeUintegerChecker<uint32_t>())

	.AddAttribute("BandwidthInterval",
				  "Bandwidth Interval",
				  TimeValue(Seconds (0)),
				  MakeTimeAccessor(&CustomPacketSink::m_bandwidthCalculationInterval),
				  MakeTimeChecker())

	.AddAttribute("ReceiverName",
				  "Receiver Name",
				  StringValue("R"),
				  MakeStringAccessor(&CustomPacketSink::m_receiverName),
				  MakeStringChecker())

    .AddTraceSource ("Rx", "A packet has been received",
                     MakeTraceSourceAccessor (&CustomPacketSink::m_rxTrace))
  ;
  return tid;
}

CustomPacketSink::CustomPacketSink ()
{
  NS_LOG_FUNCTION (this);
  m_socket = 0;
  m_totalRx = 0;

  m_bandwidth_Calculation_Event = EventId ();

  n = 20;
  m_last_n_intervals_data = new int[n];
  for (int i=0; i<n; i++) {m_last_n_intervals_data [i] = 0;}
  m_round = 0;
  m_last_totalRx = 0;
}

CustomPacketSink::~CustomPacketSink()
{
  NS_LOG_FUNCTION (this);
}

uint32_t CustomPacketSink::GetTotalRx () const
{
  NS_LOG_FUNCTION (this);
  return m_totalRx;
}

Ptr<Socket>
CustomPacketSink::GetListeningSocket (void) const
{
  NS_LOG_FUNCTION (this);
  return m_socket;
}

std::list<Ptr<Socket> >
CustomPacketSink::GetAcceptedSockets (void) const
{
  NS_LOG_FUNCTION (this);
  return m_socketList;
}

void CustomPacketSink::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_socket = 0;
  m_socketList.clear ();

  // chain up
  Application::DoDispose ();
}


// Application Methods
void CustomPacketSink::StartApplication ()    // Called at time specified by Start
{
  NS_LOG_FUNCTION (this);

  // *********************
  if (m_bandwidthCalculationInterval.GetSeconds() != 0)
  {
	  m_bandwidth_Calculation_Event = Simulator::Schedule (m_bandwidthCalculationInterval, &CustomPacketSink::CalculateBandwidth, this);
  }
  // *********************


  // Create the socket if not already
  if (!m_socket)
    {
      m_socket = Socket::CreateSocket (GetNode (), m_tid);
      m_socket->Bind (m_local);
      m_socket->Listen ();
      m_socket->ShutdownSend ();
      if (addressUtils::IsMulticast (m_local))
        {
          Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket> (m_socket);
          if (udpSocket)
            {
              // equivalent to setsockopt (MCAST_JOIN_GROUP)
              udpSocket->MulticastJoinGroup (0, m_local);
            }
          else
            {
              NS_FATAL_ERROR ("Error: joining multicast on a non-UDP socket");
            }
        }
    }

  m_socket->SetRecvCallback (MakeCallback (&CustomPacketSink::HandleRead, this));
  m_socket->SetAcceptCallback (
    MakeNullCallback<bool, Ptr<Socket>, const Address &> (),
    MakeCallback (&CustomPacketSink::HandleAccept, this));
  m_socket->SetCloseCallbacks (
    MakeCallback (&CustomPacketSink::HandlePeerClose, this),
    MakeCallback (&CustomPacketSink::HandlePeerError, this));
}

void CustomPacketSink::StopApplication ()     // Called at time specified by Stop
{
  NS_LOG_FUNCTION (this);
  while(!m_socketList.empty ()) //these are accepted sockets, close them
    {
      Ptr<Socket> acceptedSocket = m_socketList.front ();
      m_socketList.pop_front ();
      acceptedSocket->Close ();
    }
  if (m_socket) 
    {
      m_socket->Close ();
      m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
    }
}

void CustomPacketSink::HandleRead (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  Ptr<Packet> packet;
  Address from;
  while ((packet = socket->RecvFrom (from)))
    {
      if (packet->GetSize () == 0)
        { //EOF
          break;
        }
      m_totalRx += packet->GetSize ();
      if (InetSocketAddress::IsMatchingType (from))
        {
          NS_LOG_INFO ("At time " << Simulator::Now ().GetSeconds ()
                       << "s packet sink received "
                       <<  packet->GetSize () << " bytes from "
                       << InetSocketAddress::ConvertFrom(from).GetIpv4 ()
                       << " port " << InetSocketAddress::ConvertFrom (from).GetPort ()
                       << " total Rx " << m_totalRx << " bytes");
        }
      else if (Inet6SocketAddress::IsMatchingType (from))
        {
          NS_LOG_INFO ("At time " << Simulator::Now ().GetSeconds ()
                       << "s packet sink received "
                       <<  packet->GetSize () << " bytes from "
                       << Inet6SocketAddress::ConvertFrom(from).GetIpv6 ()
                       << " port " << Inet6SocketAddress::ConvertFrom (from).GetPort ()
                       << " total Rx " << m_totalRx << " bytes");
        }
      m_rxTrace (packet, from);
    }


  // *********************
  // // temporary, should be removed later
  // if (m_totalRx >= m_totalExpectedRx) {

	 //  std::cout << "Finished at " << Simulator::Now().GetSeconds() << std::endl;
	 //  Simulator::Stop();
  // }
  // *********************
}


void CustomPacketSink::HandlePeerClose (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
}
 
void CustomPacketSink::HandlePeerError (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
}
 

void CustomPacketSink::HandleAccept (Ptr<Socket> s, const Address& from)
{
  NS_LOG_FUNCTION (this << s << from);
  s->SetRecvCallback (MakeCallback (&CustomPacketSink::HandleRead, this));
  m_socketList.push_back (s);
}

//**************************************
void CustomPacketSink::CalculateBandwidth()
{
	m_bandwidth_Calculation_Event = Simulator::Schedule (m_bandwidthCalculationInterval, &CustomPacketSink::CalculateBandwidth, this);

	// Calculating bandwidth based on total perceived bandwidth
	/*std::ofstream outputT;
	outputT.open(("T-"+m_receiverName).c_str(), std::ofstream::app);
	outputT << Simulator::Now().GetSeconds() << "	" << double(m_totalRx * 8)/ (Simulator::Now().GetSeconds()) << std::endl;
	*/
  //m_output.close();

//------------------------

 	
 	// Calculating bandwidth based on last 'n' intervals
 	m_round++;
	if (m_round == n)
		m_round = 0;
	m_last_n_intervals_data [m_round] = m_totalRx - m_last_totalRx;
	m_last_totalRx = m_totalRx;

	int last_n_intervals_sum = 0;
	for (int i=0; i<n; i++)
	{
		last_n_intervals_sum = last_n_intervals_sum + m_last_n_intervals_data[i];
	}

	std::ofstream outputP;
	outputP.open(("P-"+m_receiverName).c_str(), std::ofstream::app);
  outputP << Simulator::Now().GetSeconds() << "	" << double(last_n_intervals_sum * 8)/(m_bandwidthCalculationInterval.GetSeconds() * n) << std::endl;
	
}

} // Namespace ns3
