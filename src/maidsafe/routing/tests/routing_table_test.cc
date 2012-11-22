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

#include <bitset>
#include <memory>
#include <vector>

#include "maidsafe/common/node_id.h"
#include "maidsafe/common/test.h"

#include "maidsafe/common/log.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/routing/routing_table.h"
#include "maidsafe/routing/parameters.h"
#include "maidsafe/rudp/managed_connections.h"
#include "maidsafe/routing/tests/test_utils.h"

namespace maidsafe {
namespace routing {
namespace test {

TEST(RoutingTableTest, FUNC_AddCloseNodes) {
  Fob fob;
  fob.identity = Identity(RandomString(64));
  RoutingTable routing_table(fob, false);
  NodeInfo node;
  // check the node is useful when false is set
  for (unsigned int i = 0; i < Parameters::closest_nodes_size ; ++i) {
     node.node_id = NodeId(RandomString(64));
     EXPECT_TRUE(routing_table.CheckNode(node));
  }
  EXPECT_EQ(routing_table.size(), 0);
  asymm::PublicKey dummy_key;
  // check we cannot input nodes with invalid public_keys
  for (uint16_t i = 0; i < Parameters::closest_nodes_size ; ++i) {
     NodeInfo node(MakeNode());
     node.public_key = dummy_key;
     EXPECT_FALSE(routing_table.AddNode(node));
  }
  EXPECT_EQ(0, routing_table.size());

  // everything should be set to go now
  for (uint16_t i = 0; i < Parameters::closest_nodes_size ; ++i) {
    node = MakeNode();
    EXPECT_TRUE(routing_table.AddNode(node));
  }
  EXPECT_EQ(Parameters::closest_nodes_size, routing_table.size());
}

TEST(RoutingTableTest, FUNC_AddTooManyNodes) {
  Fob fob;
  fob.identity = Identity(RandomString(64));
  RoutingTable routing_table(fob, false);
  for (uint16_t i = 0; routing_table.size() < Parameters::max_routing_table_size; ++i) {
    NodeInfo node(MakeNode());
    EXPECT_TRUE(routing_table.AddNode(node));
  }
  EXPECT_EQ(routing_table.size(), Parameters::max_routing_table_size);
  size_t count(0);
  for (uint16_t i = 0; i < 100; ++i) {
    NodeInfo node(MakeNode());
    if (routing_table.CheckNode(node)) {
      EXPECT_TRUE(routing_table.AddNode(node));
      ++count;
    }
  }
  if (count > 0)
    LOG(kInfo) << "made space for " << count << " node(s) in routing table";
  EXPECT_EQ(routing_table.size(), Parameters::max_routing_table_size);
}

TEST(RoutingTableTest, FUNC_GroupChange) {
  Fob fob;
  fob.identity = Identity(RandomString(64));
  RoutingTable routing_table(fob, false);
  std::vector<NodeInfo> nodes;
  for (uint16_t i = 0; i < Parameters::max_routing_table_size; ++i)
    nodes.push_back(MakeNode());

  SortFromTarget(routing_table.kNodeId(), nodes);

  int count(0);
  auto close_node_replaced_functor([&count](const std::vector<NodeInfo> nodes) {
    ++count;
    LOG(kInfo) << "Close node replaced. count : " << count;
    EXPECT_GE(8, count);
    for (auto i: nodes) {
      LOG(kVerbose) << "NodeId : " << DebugId(i.node_id);
    }
  });

  routing_table.InitialiseFunctors(
      [](const int& status) { LOG(kVerbose) << "Status : " << status; },
      [](const NodeInfo&, bool) {},
      close_node_replaced_functor,
      []() {});

  for (uint16_t i = 0; i < Parameters::max_routing_table_size; ++i) {
    ASSERT_TRUE(routing_table.AddNode(nodes.at(i)));
    LOG(kVerbose) << "Added to routing_table : " << DebugId(nodes.at(i).node_id);
  }

  ASSERT_EQ(routing_table.size(), Parameters::max_routing_table_size);
}

TEST(RoutingTableTest, FUNC_GetClosestNodeWithExclusion) {
  std::vector<NodeId> nodes_id;
  std::vector<std::string> exclude;
  Fob fob;
  fob.identity = Identity(RandomString(64));
  RoutingTable routing_table(fob, false);
  NodeInfo node_info;
  NodeId my_node(fob.identity);

  // Empty routing_table
  node_info = routing_table.GetClosestNode(my_node, exclude, false);
  NodeInfo node_info2(routing_table.GetClosestNode(my_node, exclude, true));
  EXPECT_EQ(node_info.node_id, node_info2.node_id);
  EXPECT_EQ(node_info.node_id, NodeInfo().node_id);

  // routing_table with one element
  NodeInfo node(MakeNode());
  nodes_id.push_back(node.node_id);
  EXPECT_TRUE(routing_table.AddNode(node));

  node_info = routing_table.GetClosestNode(my_node, exclude, false);
  node_info2 = routing_table.GetClosestNode(my_node, exclude, true);
  EXPECT_EQ(node_info.node_id, node_info2.node_id);
  node_info = routing_table.GetClosestNode(nodes_id[0], exclude, false);
  node_info2 = routing_table.GetClosestNode(nodes_id[0], exclude, true);
  EXPECT_NE(node_info.node_id, node_info2.node_id);

  exclude.push_back(nodes_id[0].string());
  node_info = routing_table.GetClosestNode(nodes_id[0], exclude, false);
  node_info2 = routing_table.GetClosestNode(nodes_id[0], exclude, true);
  EXPECT_EQ(node_info.node_id, node_info2.node_id);
  EXPECT_EQ(node_info.node_id, NodeInfo().node_id);

  // routing_table with Parameters::node_group_size elements
  exclude.clear();
  for (uint16_t i(static_cast<uint16_t>(routing_table.size()));
       routing_table.size() < Parameters::node_group_size; ++i) {
    NodeInfo node(MakeNode());
    nodes_id.push_back(node.node_id);
    EXPECT_TRUE(routing_table.AddNode(node));
  }

  node_info = routing_table.GetClosestNode(my_node, exclude, false);
  node_info2 = routing_table.GetClosestNode(my_node, exclude, true);
  EXPECT_EQ(node_info.node_id, node_info2.node_id);

  uint16_t random_index = RandomUint32() % Parameters::node_group_size;
  node_info = routing_table.GetClosestNode(nodes_id[random_index], exclude, false);
  node_info2 = routing_table.GetClosestNode(nodes_id[random_index], exclude, true);
  EXPECT_NE(node_info.node_id, node_info2.node_id);

  exclude.push_back(nodes_id[random_index].string());
  node_info = routing_table.GetClosestNode(nodes_id[random_index], exclude, false);
  node_info2 = routing_table.GetClosestNode(nodes_id[random_index], exclude, true);
  EXPECT_EQ(node_info.node_id, node_info2.node_id);

  for (auto node_id : nodes_id)
    exclude.push_back(node_id.string());
  node_info = routing_table.GetClosestNode(nodes_id[random_index], exclude, false);
  node_info2 = routing_table.GetClosestNode(nodes_id[random_index], exclude, true);
  EXPECT_EQ(node_info.node_id, node_info2.node_id);
  EXPECT_EQ(node_info.node_id, NodeInfo().node_id);

  // routing_table with Parameters::Parameters::max_routing_table_size elements
  exclude.clear();
  for (uint16_t i = static_cast<uint16_t>(routing_table.size());
       routing_table.size() < Parameters::max_routing_table_size; ++i) {
    NodeInfo node(MakeNode());
    nodes_id.push_back(node.node_id);
    EXPECT_TRUE(routing_table.AddNode(node));
  }

  node_info = routing_table.GetClosestNode(my_node, exclude, false);
  node_info2 = routing_table.GetClosestNode(my_node, exclude, true);
  EXPECT_EQ(node_info.node_id, node_info2.node_id);

  random_index = RandomUint32() % Parameters::max_routing_table_size;
  node_info = routing_table.GetClosestNode(nodes_id[random_index], exclude, false);
  node_info2 = routing_table.GetClosestNode(nodes_id[random_index], exclude, true);
  EXPECT_NE(node_info.node_id, node_info2.node_id);

  exclude.push_back(nodes_id[random_index].string());
  node_info = routing_table.GetClosestNode(nodes_id[random_index], exclude, false);
  node_info2 = routing_table.GetClosestNode(nodes_id[random_index], exclude, true);
  EXPECT_EQ(node_info.node_id, node_info2.node_id);

  for (auto node_id : nodes_id)
    exclude.push_back(node_id.string());
  node_info = routing_table.GetClosestNode(nodes_id[random_index], exclude, false);
  node_info2 = routing_table.GetClosestNode(nodes_id[random_index], exclude, true);
  EXPECT_EQ(node_info.node_id, node_info2.node_id);
  EXPECT_EQ(node_info.node_id, NodeInfo().node_id);
}

TEST(RoutingTableTest, GetRemovableNode) {
  std::vector<NodeId> node_ids;
  Fob fob;
  std::string random_string(RandomString(64));
  fob.identity = Identity(random_string);
  RoutingTable routing_table(fob, false);
  for (uint16_t i = 0; routing_table.size() < Parameters::max_routing_table_size; ++i) {
    NodeInfo node(MakeNode());
    node_ids.push_back(node.node_id);
    EXPECT_TRUE(routing_table.AddNode(node));
  }
  EXPECT_EQ(routing_table.size(), Parameters::max_routing_table_size);
  size_t count(0);
  for (uint16_t i = 0; i < 70; ++i) {
    NodeInfo node(MakeNode());
    if (routing_table.CheckNode(node)) {
      node_ids.push_back(node.node_id);
      EXPECT_TRUE(routing_table.AddNode(node));
      ++count;
    }
  }
  std::vector<NodeId> close_buckets;
  for (uint16_t i = 0; i < 30; ++i) {
    NodeInfo node(MakeNode());
    node.node_id = GenerateUniqueRandomId(NodeId(random_string), 100);
    if (routing_table.CheckNode(node)) {
      close_buckets.push_back(node.node_id);
      EXPECT_TRUE(routing_table.AddNode(node));
      ++count;
    }
  }

  NodeInfo removed_node;
  removed_node = routing_table.GetRemovableNode();
  EXPECT_GE(removed_node.bucket, 510);
  std::vector<std::string> attempted_nodes;
  for (size_t index(0);  index < 10; ++index) {
    removed_node = routing_table.GetRemovableNode(attempted_nodes);
    EXPECT_EQ(std::find(attempted_nodes.begin(),
                        attempted_nodes.end(),
                        removed_node.node_id.string()), attempted_nodes.end());
    attempted_nodes.push_back(removed_node.node_id.string());
  }
}

}  // namespace test
}  // namespace routing
}  // namespace maidsafe
