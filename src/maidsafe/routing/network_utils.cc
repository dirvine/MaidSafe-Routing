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

#include "maidsafe/routing/network_utils.h"

#include "boost/date_time/posix_time/posix_time_config.hpp"

#include "maidsafe/common/log.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/rudp/return_codes.h"

#include "maidsafe/routing/bootstrap_file_handler.h"
#include "maidsafe/routing/non_routing_table.h"
#include "maidsafe/routing/parameters.h"
#include "maidsafe/routing/return_codes.h"
#include "maidsafe/routing/routing_pb.h"
#include "maidsafe/routing/routing_table.h"
#include "maidsafe/routing/utils.h"

namespace bptime = boost::posix_time;

namespace maidsafe {

namespace {

typedef boost::asio::ip::udp::endpoint Endpoint;
typedef boost::shared_lock<boost::shared_mutex> SharedLock;
typedef boost::unique_lock<boost::shared_mutex> UniqueLock;

}  // anonymous namespace

namespace routing {

NetworkUtils::NetworkUtils(RoutingTable& routing_table, NonRoutingTable& non_routing_table)
    : running_(true),
      running_mutex_(),
      bootstrap_attempt_(0),
      bootstrap_endpoints_(),
      bootstrap_connection_id_(),
      this_node_relay_connection_id_(),
      routing_table_(routing_table),
      non_routing_table_(non_routing_table),
      nat_type_(rudp::NatType::kUnknown),
      new_bootstrap_endpoint_(),
      rudp_() {}

NetworkUtils::~NetworkUtils() {
  std::lock_guard<std::mutex> lock(running_mutex_);
  running_ = false;
}

int NetworkUtils::Bootstrap(const std::vector<Endpoint>& bootstrap_endpoints,
                            const rudp::MessageReceivedFunctor& message_received_functor,
                            const rudp::ConnectionLostFunctor& connection_lost_functor,
                            Endpoint local_endpoint) {
  {
    std::lock_guard<std::mutex> lock(running_mutex_);
    if (!running_)
      return kNetworkShuttingDown;
  }

  assert(connection_lost_functor && "Must provide a valid functor");
  assert(bootstrap_connection_id_.IsZero() && "bootstrap_connection_id_ must be empty");
  std::shared_ptr<asymm::PrivateKey>
      private_key(new asymm::PrivateKey(routing_table_.kFob().keys.private_key));
  std::shared_ptr<asymm::PublicKey>
      public_key(new asymm::PublicKey(routing_table_.kFob().keys.public_key));

  if (!bootstrap_endpoints.empty())
    bootstrap_endpoints_ = bootstrap_endpoints;

  if (Parameters::append_maidsafe_endpoints && bootstrap_attempt_ == 0) {
    LOG(kInfo) << "Appending Maidsafe Endpoints";
    std::vector<Endpoint> maidsafe_endpoints(MaidSafeEndpoints());
    bootstrap_endpoints_.insert(bootstrap_endpoints_.end(), maidsafe_endpoints.begin(),
                                maidsafe_endpoints.end());
  } else if (Parameters::append_maidsafe_local_endpoints && bootstrap_attempt_ == 0) {
    std::vector<Endpoint> maidsafe_local_endpoints(MaidSafeLocalEndpoints());
    bootstrap_endpoints_.insert(bootstrap_endpoints_.end(), maidsafe_local_endpoints.begin(),
                                maidsafe_local_endpoints.end());
  }

  if (bootstrap_endpoints_.empty())
    return kInvalidBootstrapContacts;

  int result(rudp_.Bootstrap(/* sorted_ */bootstrap_endpoints_,
                             message_received_functor,
                             connection_lost_functor,
                             routing_table_.kConnectionId(),
                             private_key,
                             public_key,
                             bootstrap_connection_id_,
                             nat_type_,
                             local_endpoint));
  ++bootstrap_attempt_;
  // RUDP will return a kZeroId for zero state !!
  if (result != kSuccess || bootstrap_connection_id_.IsZero()) {
    LOG(kError) << "No Online Bootstrap Node found.";
    return kNoOnlineBootstrapContacts;
  }

  this_node_relay_connection_id_ = routing_table_.kConnectionId();
  LOG(kInfo) << "Bootstrap successful, bootstrap connection id - "
             << DebugId(bootstrap_connection_id_);
  return kSuccess;
}

int NetworkUtils::GetAvailableEndpoint(const NodeId& peer_id,
                                       const rudp::EndpointPair& peer_endpoint_pair,
                                       rudp::EndpointPair& this_endpoint_pair,
                                       rudp::NatType& this_nat_type) {
  {
    std::lock_guard<std::mutex> lock(running_mutex_);
    if (!running_)
      return kNetworkShuttingDown;
  }
  return rudp_.GetAvailableEndpoint(peer_id, peer_endpoint_pair, this_endpoint_pair, this_nat_type);
}

int NetworkUtils::Add(const NodeId& peer_id,
                      const rudp::EndpointPair& peer_endpoint_pair,
                      const std::string& validation_data) {
  {
    std::lock_guard<std::mutex> lock(running_mutex_);
    if (!running_)
      return kNetworkShuttingDown;
  }
  return rudp_.Add(peer_id, peer_endpoint_pair, validation_data);
}

int NetworkUtils::MarkConnectionAsValid(const NodeId& peer_id) {
  {
    std::lock_guard<std::mutex> lock(running_mutex_);
    if (!running_)
      return kNetworkShuttingDown;
  }
  Endpoint new_bootstrap_endpoint;
  int ret_val(rudp_.MarkConnectionAsValid(peer_id, new_bootstrap_endpoint));
  if ((ret_val == kSuccess) && !new_bootstrap_endpoint.address().is_unspecified()) {
    LOG(kVerbose) << "Found usable endpoint for bootstrapping : " << new_bootstrap_endpoint;
    // TODO(Prakash): Is separate thread needed here ?
    if (new_bootstrap_endpoint_)
      new_bootstrap_endpoint_(new_bootstrap_endpoint);
  }
  return ret_val;
}

void NetworkUtils::Remove(const NodeId& peer_id) {
  {
    std::lock_guard<std::mutex> lock(running_mutex_);
    if (!running_)
      return;
  }
  rudp_.Remove(peer_id);
}

void NetworkUtils::RudpSend(const NodeId& peer_id,
                            const protobuf::Message& message,
                            const rudp::MessageSentFunctor& message_sent_functor) {
  {
    std::lock_guard<std::mutex> lock(running_mutex_);
    if (!running_)
      return;
  }
  rudp_.Send(peer_id, message.SerializeAsString(), message_sent_functor);
}

void NetworkUtils::SendToDirect(const protobuf::Message& message,
                                const NodeId& peer_connection_id,
                                const rudp::MessageSentFunctor& message_sent_functor) {
  RudpSend(peer_connection_id, message, message_sent_functor ? message_sent_functor : nullptr);
}

void NetworkUtils::SendToDirect(const protobuf::Message& message,
                                const NodeId& peer_node_id,
                                const NodeId& peer_connection_id) {
  SendTo(message, peer_node_id, peer_connection_id);
}

void NetworkUtils::SendToClosestNode(const protobuf::Message& message) {
  // Normal messages
  if (message.has_destination_id() && !message.destination_id().empty()) {
    auto non_routing_nodes(non_routing_table_.GetNodesInfo(NodeId(message.destination_id())));
    // have the destination ID in non-routing table
    if (!non_routing_nodes.empty() && message.direct()) {
      if (IsRequest(message) &&
          (!message.client_node() ||
           (message.source_id() != message.destination_id()))) {
        LOG(kWarning) << "This node [" << HexSubstr(routing_table_.kFob().identity)
                      << " Dropping message as non-client to client message not allowed."
                      << PrintMessage(message);
        return;
      }
      LOG(kVerbose) << "This node [" << DebugId(routing_table_.kNodeId()) << "] has "
                    << non_routing_nodes.size() << " destination node(s) in its non-routing table."
                    << " id: " << message.id();

      for (auto i : non_routing_nodes) {
        LOG(kVerbose) << "Sending message to NRT node with ID " << message.id();
        SendTo(message, i.node_id, i.connection_id);
      }
    } else if (routing_table_.size() > 0) {  // getting closer nodes from routing table
      RecursiveSendOn(message);
    } else {
      LOG(kError) << " No endpoint to send to; aborting send.  Attempt to send a type "
                  << MessageTypeString(message) << " message to " << HexSubstr(message.source_id())
                  << " from " << HexSubstr(routing_table_.kFob().identity)
                  << " id: " << message.id();
    }
    return;
  }

  // Relay message responses only
  if (message.has_relay_id() && (IsResponse(message))) {
    protobuf::Message relay_message(message);
    relay_message.set_destination_id(message.relay_id());  // so that peer identifies it as direct
    SendTo(relay_message, NodeId(relay_message.relay_id()),
           NodeId(relay_message.relay_connection_id()));
  } else {
    LOG(kError) << "Unable to work out destination; aborting send." << " id: " << message.id()
                << " message.has_relay_id() ; " << std::boolalpha << message.has_relay_id()
                << " Isresponse(message) : " << std::boolalpha << IsResponse(message)
                << " message.has_relay_connection_id() : "
                << std::boolalpha << message.has_relay_connection_id();
  }
}

void NetworkUtils::SendTo(const protobuf::Message& message,
                          const NodeId& peer_node_id,
                          const NodeId& peer_connection_id) {
  const std::string kThisId(routing_table_.kNodeId().string());
  rudp::MessageSentFunctor message_sent_functor = [=](int message_sent) {
      if (rudp::kSuccess == message_sent) {
        LOG(kInfo) << "  [" << HexSubstr(kThisId) << "] sent : " << MessageTypeString(message)
                   << " to   " << DebugId(peer_node_id) << "   (id: " << message.id() << ")";
      } else {
        LOG(kError) << "Sending type " << MessageTypeString(message) << " message from "
                    << HexSubstr(kThisId) << " to " << DebugId(peer_node_id) << " failed with code "
                    << message_sent << " id: " << message.id();
      }
    };
  LOG(kVerbose) << " >>>>>>>>> rudp send message to connection id " << DebugId(peer_connection_id);
  RudpSend(peer_connection_id, message, message_sent_functor);
}

void NetworkUtils::RecursiveSendOn(protobuf::Message message,
                                   NodeInfo last_node_attempted,
                                   int attempt_count) {
  {
    std::lock_guard<std::mutex> lock(running_mutex_);
    if (!running_)
      return;
  }
  if (attempt_count >= 3) {
    LOG(kWarning) << " Retry attempts failed to send to ["
                  << HexSubstr(last_node_attempted.node_id.string())
                  << "] will drop this node now and try with another node."
                  << " id: " << message.id();
    attempt_count = 0;
    {
      std::lock_guard<std::mutex> lock(running_mutex_);
      if (!running_)
        return;
      rudp_.Remove(last_node_attempted.connection_id);
      LOG(kWarning) << " Routing -> removing connection " << last_node_attempted.node_id.string();
      // FIXME Should we remove this node or let rudp handle that?
      routing_table_.DropNode(last_node_attempted.connection_id, false);
      non_routing_table_.DropConnection(last_node_attempted.connection_id);
    }
  }

  if (attempt_count > 0)
    Sleep(bptime::milliseconds(50));

  const std::string kThisId(routing_table_.kNodeId().string());
  bool ignore_exact_match(!IsDirect(message));
  std::vector<std::string> route_history;
  NodeInfo closest_node;
  {
    std::lock_guard<std::mutex> lock(running_mutex_);
    if (!running_)
      return;
    if (message.route_history().size() > 1)
      route_history = std::vector<std::string>(message.route_history().begin(),
                                               message.route_history().end());
    else if ((message.route_history().size() == 1) &&
             (message.route_history(0) != routing_table_.kFob().identity.string()))
      route_history.push_back(message.route_history(0));

    closest_node = routing_table_.GetClosestNode(NodeId(message.destination_id()), route_history,
                                                 ignore_exact_match);
    if (closest_node.node_id == NodeId()) {
      LOG(kError) << "This node's routing table is empty now.  Need to re-bootstrap.";
      return;
    }

    AdjustRouteHistory(message);
  }

  rudp::MessageSentFunctor message_sent_functor = [=](int message_sent) {
      {
        std::lock_guard<std::mutex> lock(running_mutex_);
        if (!running_)
          return;
      }
      if (rudp::kSuccess == message_sent) {
        LOG(kInfo) << "  [" << HexSubstr(kThisId) << "] sent : "
                   << MessageTypeString(message) << " to   "
                   << HexSubstr(closest_node.node_id.string())
                   << "   (id: " << message.id() << ")"
                   << " dst : " << HexSubstr(message.destination_id());
      } else if (rudp::kSendFailure == message_sent) {
        LOG(kError) << "Sending type " << MessageTypeString(message)
                    << " message from " << HexSubstr(routing_table_.kNodeId().string())
                    << " to " << HexSubstr(closest_node.node_id.string())
                    << " with destination ID " << HexSubstr(message.destination_id())
                    << " failed with code " << message_sent
                    << ".  Will retry to Send.  Attempt count = " << attempt_count + 1
                    << " id: " << message.id();
        RecursiveSendOn(message, closest_node, attempt_count + 1);
      } else {
        LOG(kError) << "Sending type " << MessageTypeString(message) << " message from "
                    << HexSubstr(kThisId) << " to " << HexSubstr(closest_node.node_id.string())
                    << " with destination ID " << HexSubstr(message.destination_id())
                    << " failed with code " << message_sent << "  Will remove node."
                    << " message id: " << message.id();
        {
          std::lock_guard<std::mutex> lock(running_mutex_);
          if (!running_)
            return;
          rudp_.Remove(last_node_attempted.connection_id);
        }
        LOG(kWarning) << " Routing-> removing connection " << DebugId(closest_node.connection_id);
        routing_table_.DropNode(closest_node.node_id, false);
        non_routing_table_.DropConnection(closest_node.connection_id);
        RecursiveSendOn(message);
      }
  };
  LOG(kVerbose) << "Rudp recursive send message to " << DebugId(closest_node.connection_id);
  RudpSend(closest_node.connection_id, message, message_sent_functor);
}

void NetworkUtils::AdjustRouteHistory(protobuf::Message& message) {
  assert(message.route_history().size() <= Parameters::max_routing_table_size);
  if (std::find(message.route_history().begin(), message.route_history().end(),
                routing_table_.kFob().identity.string()) == message.route_history().end()) {
    message.add_route_history(routing_table_.kFob().identity.string());
    if (message.route_history().size() > Parameters::max_route_history) {
      std::vector<std::string> route_history(message.route_history().begin() + 1,
                                             message.route_history().end());
      message.clear_route_history();
      for (auto route : route_history) {
        if (!NodeId(route).IsZero())
          message.add_route_history(route);
      }
    }
  }
  assert(message.route_history().size() <= Parameters::max_routing_table_size);
}

void NetworkUtils::set_new_bootstrap_endpoint_functor(
    NewBootstrapEndpointFunctor new_bootstrap_endpoint) {
  new_bootstrap_endpoint_ = new_bootstrap_endpoint;
}

void NetworkUtils::clear_bootstrap_connection_info() {
  bootstrap_connection_id_ = NodeId();
  this_node_relay_connection_id_ = NodeId();
}

maidsafe::NodeId NetworkUtils::bootstrap_connection_id() const {
  if (running_)
    return bootstrap_connection_id_;
  return NodeId();
}

maidsafe::NodeId NetworkUtils::this_node_relay_connection_id() const {
  return this_node_relay_connection_id_;
}

rudp::NatType NetworkUtils::nat_type() const {
  return nat_type_;
}

}  // namespace routing

}  // namespace maidsafe
