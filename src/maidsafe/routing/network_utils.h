/*******************************************************************************
 *  Copyright 2012 maidsafe.net limited                                        *
 *                                                                             *
 *  The following source code is property of maidsafe.net limited and is not   *
 *  meant for external use.  The use of this code is governed by the licence   *
 *  file licence.txt found in the root of this directory and also on           *
 *  www.maidsafe.net.                                                          *
 *                                                                             *
 *  You are not free to copy, amend or otherwise use this source code without  *
 *  the explicit written permission of the board of directors of maidsafe.net. *
 ******************************************************************************/

#ifndef MAIDSAFE_ROUTING_NETWORK_UTILS_H_
#define MAIDSAFE_ROUTING_NETWORK_UTILS_H_

#include <atomic>

#include "maidsafe/rudp/managed_connections.h"
#include "maidsafe/routing/node_info.h"

namespace maidsafe {

namespace rudp {class ManagedConnections; } // namespace rudp

namespace routing {

namespace protobuf { class Message;}  // namespace protobuf

class NonRoutingTable;
class RoutingTable;

//  Message processing evaluation order
// 1. If valid parameter - endpoint is provided, the message is sent once to it. On failure,
//    it does nothing.
// 2. If message has destination id, message is sent to the right destination or to node close to
//    destination id. On failure, it retries to send until it tries with all its RT nodes.
// 3. If message has relay id & endpoint and its a response type message, the NRT is seeked for
//    the relay id. If relay id exist in NRT, it is send to that node. Or else, message is sent to
//    the relay_endpoint provided in the message. On failure, it does nothing.
// Note: For sending relay requests, message with empty source id may be provided, along with
// direct endpint if RT is empty.
void ProcessSend(protobuf::Message message,
                 rudp::ManagedConnections &rudp,
                 RoutingTable &routing_table,
                 NonRoutingTable &non_routing_table,
                 Endpoint peer_endpoint = Endpoint());

class NetworkUtils {
 public:
  NetworkUtils(RoutingTable &routing_table,
               NonRoutingTable &non_routing_table);

  Endpoint Bootstrap(const std::vector<Endpoint> &bootstrap_endpoints,
                     rudp::MessageReceivedFunctor message_received_functor,
                     rudp::ConnectionLostFunctor connection_lost_functor,
                     Endpoint local_endpoint = Endpoint());

  int GetAvailableEndpoint(const Endpoint &peer_endpoint,
                           rudp::EndpointPair &this_endpoint_pair);

  int Add(const Endpoint &this_endpoint,
          const Endpoint &peer_endpoint,
          const std::string &validation_data);

  void Remove(const Endpoint &peer_endpoint);

// For sending relay requests, message with empty source id may be provided, along with
// direct endpint.
  void SendToDirectEndpoint(protobuf::Message message,
                            Endpoint direct_endpoint);

  void SendToDirectEndpoint(protobuf::Message message,
                            Endpoint direct_endpoint,
                            rudp::MessageSentFunctor message_sent_functor);

  // Handles relay response messages also
  // leave destination id empty if needs to send as a relay response message
  void SendToClosestNode(protobuf::Message message);

private:
  void SendTo(protobuf::Message message,
              const NodeId &node_id,
              const Endpoint &endpoint);

  void RecursiveSendOn(protobuf::Message message,
                       NodeInfo last_node_attempted = NodeInfo(),
                       int attempt_count = 0);
  //  actual rudp send
  void RudpSend(protobuf::Message message,
                Endpoint endpoint,
                rudp::MessageSentFunctor message_sent_functor);

  void SignMessage(protobuf::Message &message);

  RoutingTable &routing_table_;
  NonRoutingTable &non_routing_table_;
  rudp::ManagedConnections rudp_;
  std::atomic<bool> tearing_down_;
};

}  // namespace routing

}  // namespace maidsafe

#endif  // MAIDSAFE_ROUTING_NETWORK_UTILS_H_