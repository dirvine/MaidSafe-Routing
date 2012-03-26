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

/*
Guarantees
__________

1:  Find any node by key.
2:  Find any value by key.
3:  Ensure messages are sent to all closest nodes in order (close to furthest).
4:  Provide NAT traversal techniques where necessary.
5:  Read and Write configuration file to allow bootstrap from known nodes.
6:  Read and Write configuration file preserving ID and private key.
7:  Allow retrieval of bootstrap nodes from known location.
8:  Remove bad nodes from all routing tables (ban from network).
9:  Inform of close node changes in routing table.
10: Respond to every send that requires it, either with timeout or reply

Client connects with a made up address (which must be unique (he can remember it))
but requres to sign with a valid PMID
Connects to closest nodes plus some others (no need to update much)
all returns will come through close nodes mostly unless we allow
by proxy which I think we may.
We can detect a client and wrap his message in one of ours
this can make sure they can only do certain things as well (no accounts/CIH etc.)
we can register client acceptable messages or node accptable and allow
clients anything else ?
if a client or hacker tries to start as a node ID it will not be unique
  we can use dans signal block to make sure clients only get signalled client stuff
  this will help is we release a public API and soembody tries to get smart
  (help not solve)
  The node passing the message back knows its a client he is talking to and if the message is a node
  type he can drop it.
*/

#ifndef MAIDSAFE_ROUTING_ROUTING_API_H_
#define MAIDSAFE_ROUTING_ROUTING_API_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>  // for pair
#include <map>
#include <vector>
#include "boost/signals2/signal.hpp"
#include "boost/filesystem/path.hpp"
#include "maidsafe/common/rsa.h"
#include "maidsafe/transport/managed_connections.h"
#include "maidsafe/routing/version.h"

#if MAIDSAFE_ROUTING_VERSION != 100
#  error This API is not compatible with the installed library.\
  Please update the maidsafe_routing library.
#endif

namespace maidsafe {

namespace routing {

class MessageHandler;


namespace protobuf { class Message; }

class RoutingTable;
class NodeId;
class NodeInfo;
class Service;
class Rpcs;
class Timer;
class CacheManager;
class ResponseHandler;

struct Message {
 public:
  Message();
  explicit Message(const protobuf::Message &protobuf_message);
  int32_t type;
  std::string source_id;
  std::string destination_id;
  std::string data;
  bool timeout;
  bool cacheable;
  bool direct;
  int32_t replication;
};

typedef std::function<void(int /*message type*/,
                           std::string /*message*/ )> ResponseReceivedFunctor;
typedef std::function<void(const std::string& /*node Id*/ ,
                           const transport::Endpoint& /*Node endpoint */,
                           const bool)/*client ? */>  NodeValidationFunctor;

class Routing {
 public:
  Routing(bool client_mode,
          const asymm::Keys &keys,
          bool encryption_required);
  ~Routing();
  void BootStrapFromThisEndpoint(const maidsafe::transport::Endpoint& endpoint);
  int Send(const Message &message,
            const ResponseReceivedFunctor response_functor);
  // this object will not start unless this functor is set !!
  // this functor MUST call ValideThisNode with result
  void setNodeValidationFunctor(NodeValidationFunctor &node_validation_functor);
  // on completion of above functor this method MUST be passed the results
  void ValidateThisNode(const std::string &node_id,
                        const asymm::PublicKey &public_key,
                        const transport::Endpoint &endpoint,
                        bool client);
  boost::signals2::signal<void(int, std::string)> &MessageReceivedSignal();
  boost::signals2::signal<void(unsigned int)> &NetworkStatusSignal();
  boost::signals2::signal<void(std::string, std::string)>
                                           &CloseNodeReplacedOldNewSignal();
 private:
  Routing(const Routing&);  // no copy
  Routing& operator=(const Routing&);  // no assign
  void Init();
  void Join();
  void ReceiveMessage(const std::string &message);
  AsioService asio_service_;
  std::vector<transport::Endpoint> bootstrap_nodes_;
  asymm::Keys keys_;
  transport::Endpoint node_local_endpoint_;
  transport::Endpoint node_external_endpoint_;
  std::shared_ptr<transport::ManagedConnections> transport_;
  std::shared_ptr<RoutingTable> routing_table_;
  std::shared_ptr<Timer> timer_;
  std::shared_ptr<MessageHandler> message_handler_;
  boost::signals2::signal<void(int, std::string)> message_received_signal_;
  boost::signals2::signal<void(unsigned int)> network_status_signal_;
  boost::signals2::signal<void(std::string, std::string)>
                                                    close_node_from_to_signal_;

  std::map<uint32_t, std::pair<std::shared_ptr<boost::asio::deadline_timer>,
                              ResponseReceivedFunctor> > waiting_for_response_;
  std::vector<NodeInfo> client_connections_;  // hold connections to clients only
  std::vector<NodeInfo> client_routing_table_;  // when node is client this is
  // closest nodes to the client.
  bool joined_;
  bool encryption_required_;
  bool client_mode_;
  NodeValidationFunctor node_validation_functor_;
};

}  // namespace routing

}  // namespace maidsafe

#endif  // MAIDSAFE_ROUTING_ROUTING_API_H_
